/** 双通道分流器实现 — v2 二进制自同步帧协议
 *
 * v1 按行解析: JPEG 二进制含 0x0A → 一旦错过帧头(如波特率竞态)永远失步,
 *     载荷被当文字灌给引擎, 屏幕永不更新 (2026-09-09 实测根因之二)。
 * v2: 在字节流任意位置扫描 "FRAME:" 魔数 → 读 8 位 hex 长度 → 读载荷。
 *     误同步概率 ≈ 2^-48, 失步最多损失一帧。
 * 文本行 (CTRL:/其他) 仍按 \n 分行派发 (兼容控制台指令通道)。
 */
#include "channel_demux.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "demux";

static transport_t      *g_transport;
static frame_handler_t   g_frame_handler;
static ctrl_handler_t    g_ctrl_handler;
static raw_line_handler_t g_raw_handler;
static void              *g_frame_ctx;
static void              *g_ctrl_ctx;
static void              *g_raw_ctx;

/* 行缓冲 — 最多 1023 字节一行 + 500ms 超时保护 (仅 TEXT 态使用) */
static char    g_line_buf[1024];
static size_t  g_line_pos;
static int64_t g_line_start_us;  /* 行开始时间, 0=空闲 */

/* JPEG 帧重组缓冲 — 最大 64KB */
static uint8_t *g_jpeg_buf = NULL;
static size_t   g_jpeg_buf_size = 64 * 1024;

/* ── 帧扫描状态机 ── */
#define FRAME_MAGIC      "FRAME:"
#define FRAME_MAGIC_LEN  6
#define FRAME_HEX_LEN    8

typedef enum { DM_TEXT = 0, DM_SIZE, DM_PAYLOAD, DM_DROP } dm_state_t;
static dm_state_t g_state = DM_TEXT;
static char     g_magic_win[FRAME_MAGIC_LEN];
static char     g_hex[FRAME_HEX_LEN];
static size_t   g_hex_pos = 0;
static uint32_t g_frame_size = 0;
static size_t   g_payload_got = 0;
static size_t   g_drop_left = 0;

static int read_n(uint8_t *dst, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int chunk = g_transport->recv(g_transport, dst + got, n - got, 100);
        if (chunk <= 0) break;
        got += chunk;
    }
    return (got == n) ? 0 : -1;
}

/* ── 内部: 文本行完成 (CTRL/原始行派发) ── */
static void text_line_complete(void)
{
    if (g_line_pos == 0) return;
    if (g_line_buf[g_line_pos - 1] == '\r') g_line_pos--;
    g_line_buf[g_line_pos] = '\0';
    g_line_pos = 0;
    g_line_start_us = 0;

    if (strncmp(g_line_buf, "CTRL:", 5) == 0) {
        if (g_ctrl_handler)
            g_ctrl_handler(g_line_buf + 5, g_ctrl_ctx);
    } else {
        if (g_raw_handler)
            g_raw_handler(g_line_buf, g_raw_ctx);
    }
}

/* ── 内部: TEXT 态单字节 (行解析 + 魔数滚动窗口) ── */
static void dm_text_byte(uint8_t ch)
{
    /* 魔数滚动窗口: 行首起 6 字节匹配 "FRAME:" → 进入长度态 */
    if (g_line_pos < FRAME_MAGIC_LEN) {
        g_magic_win[g_line_pos] = (char)ch;
    } else {
        memmove(g_magic_win, g_magic_win + 1, FRAME_MAGIC_LEN - 1);
        g_magic_win[FRAME_MAGIC_LEN - 1] = (char)ch;
    }
    if (g_line_pos >= FRAME_MAGIC_LEN - 1 &&
        memcmp(g_magic_win, FRAME_MAGIC, FRAME_MAGIC_LEN) == 0) {
        g_line_pos = 0;                       /* 丢弃行缓冲里的 "FRAME" 前缀 */
        g_line_start_us = 0;
        g_hex_pos = 0;
        g_state = DM_SIZE;
        return;
    }

    if (ch == '\n') {
        if (g_line_pos > 0) {
            if (g_line_buf[g_line_pos - 1] == '\r') g_line_pos--;
            g_line_buf[g_line_pos] = '\0';
            text_line_complete();
            g_line_pos = 0;
            g_line_start_us = 0;
        }
        return;
    }
    if (g_line_pos < sizeof(g_line_buf) - 1) {
        if (g_line_pos == 0) g_line_start_us = esp_timer_get_time();
        g_line_buf[g_line_pos++] = (char)ch;
    }
    /* 行溢出: 静默丢弃, 等下一个 \n */
}

/* ── 内部: 单字节状态机入口 ── */
static void dm_feed(uint8_t ch)
{
    switch (g_state) {
    case DM_TEXT:
        dm_text_byte(ch);
        break;

    case DM_SIZE:
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            if (g_hex_pos < FRAME_HEX_LEN) g_hex[g_hex_pos++] = (char)ch;
        } else {
            /* 长度字段结束符 (\n) */
            if (g_hex_pos == FRAME_HEX_LEN) {
                g_frame_size = 0;
                for (size_t k = 0; k < FRAME_HEX_LEN; k++) {
                    char c = g_hex[k];
                    g_frame_size <<= 4;
                    if (c >= '0' && c <= '9') g_frame_size |= (uint32_t)(c - '0');
                    else if (c >= 'a' && c <= 'f') g_frame_size |= (uint32_t)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') g_frame_size |= (uint32_t)(c - 'A' + 10);
                }
                if (g_frame_size == 0 || g_frame_size > g_jpeg_buf_size) {
                    ESP_LOGW(TAG, "Frame size invalid: %" PRIu32, g_frame_size);
                    g_drop_left = g_frame_size;   /* 丢弃该帧载荷 */
                    g_state = DM_DROP;
                } else {
                    g_payload_got = 0;
                    g_state = DM_PAYLOAD;
                }
            } else {
                ESP_LOGW(TAG, "Short hex header (%d)", (int)g_hex_pos);
                g_state = DM_TEXT;
            }
            g_line_pos = 0;
            g_line_start_us = 0;
        }
        break;

    case DM_PAYLOAD:
        g_jpeg_buf[g_payload_got++] = ch;
        if (g_payload_got >= g_frame_size) {
            if (g_frame_handler)
                g_frame_handler(g_jpeg_buf, g_frame_size, g_frame_ctx);
            g_state = DM_TEXT;
            g_line_pos = 0;
            g_line_start_us = 0;
        }
        break;

    case DM_DROP:
        if (--g_drop_left == 0) {
            g_state = DM_TEXT;
            g_line_pos = 0;
            g_line_start_us = 0;
        }
        break;
    }
}

/* ── 公开 API ── */

void channel_demux_init(transport_t *transport)
{
    g_transport = transport;
    g_line_pos  = 0;
    g_state     = DM_TEXT;
    memset(g_magic_win, 0, sizeof(g_magic_win));
    g_jpeg_buf  = (uint8_t *)malloc(g_jpeg_buf_size);
    if (!g_jpeg_buf) {
        ESP_LOGE(TAG, "JPEG buffer malloc failed");
    }
    ESP_LOGI(TAG, "Channel demuxer v2 ready (binary sync, jpeg_buf=%d)", (int)g_jpeg_buf_size);
}

void channel_demux_set_frame_handler(frame_handler_t handler, void *ctx)
{
    g_frame_handler = handler;
    g_frame_ctx     = ctx;
}

void channel_demux_set_ctrl_handler(ctrl_handler_t handler, void *ctx)
{
    g_ctrl_handler = handler;
    g_ctrl_ctx     = ctx;
}

void channel_demux_set_raw_handler(raw_line_handler_t handler, void *ctx)
{
    g_raw_handler = handler;
    g_raw_ctx     = ctx;
}

void channel_demux_poll(void)
{
    if (!g_transport || !g_jpeg_buf) return;

    /* TEXT 态行超时: 500ms 无 \n → 重置行缓冲 */
    if (g_state == DM_TEXT && g_line_pos > 0 && g_line_start_us > 0) {
        if (esp_timer_get_time() - g_line_start_us > 500000) {
            ESP_LOGW(TAG, "Line timeout, reset (%d bytes dropped)", (int)g_line_pos);
            g_line_pos = 0;
            g_line_start_us = 0;
        }
    }

    /* 批量排空: 每次 poll 最多 16x512B, 逐字节喂状态机。
     * 旧版每 poll 只读 1 字节 ≈ 100B/s, 2M 链路 200KB/s 溢出丢帧。
     * 大载荷 (BLOCK>512) 时转 read_n 批量收。 */
    uint8_t chunk[512];
    int rounds = 0;
    while (rounds < 16) {
        int n = g_transport->recv(g_transport, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        rounds++;
        for (int i = 0; i < n; i++) dm_feed(chunk[i]);
        if (g_state == DM_PAYLOAD && g_frame_size > 256) {
            size_t need = g_frame_size - g_payload_got;
            if (read_n(g_jpeg_buf + g_payload_got, need) == 0)
                g_payload_got = g_frame_size;
            if (g_payload_got >= g_frame_size) {
                if (g_frame_handler)
                    g_frame_handler(g_jpeg_buf, g_frame_size, g_frame_ctx);
                g_state = DM_TEXT;
                g_line_pos = 0;
                g_line_start_us = 0;
            } else {
                break;  /* 载荷未到齐, 下次 poll 继续 */
            }
        }
    }
}
