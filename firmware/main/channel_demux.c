/** 双通道分流器实现 */
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

/* 行缓冲 — 最多 1023 字节一行 + 500ms 超时保护 */
static char    g_line_buf[1024];
static size_t  g_line_pos;
static int64_t g_line_start_us;  /* 行开始时间, 0=空闲 */

/* JPEG 帧重组缓冲 — 最大 64KB */
static uint8_t *g_jpeg_buf = NULL;
static size_t   g_jpeg_buf_size = 64 * 1024;

/* ── 内部: 从 transport 读 N 字节 (阻塞) ── */
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

/* ── 内部: 处理 FRAME 行 ── */
static void handle_frame_line(void)
{
    if (!g_frame_handler) return;

    /* 解析 hex size */
    uint32_t jpeg_size = 0;
    for (int k = 6; k < 14 && g_line_buf[k]; k++) {
        jpeg_size <<= 4;
        char c = g_line_buf[k];
        if (c >= '0' && c <= '9')      jpeg_size |= (c - '0');
        else if (c >= 'a' && c <= 'f') jpeg_size |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') jpeg_size |= (c - 'A' + 10);
        else break;
    }

    if (jpeg_size == 0 || jpeg_size > g_jpeg_buf_size) {
        ESP_LOGW(TAG, "Frame size invalid: %" PRIu32, jpeg_size);
        return;
    }

    /* 读取 JPEG 数据 */
    if (read_n(g_jpeg_buf, jpeg_size) != 0) {
        ESP_LOGW(TAG, "Frame read incomplete: got %d of %" PRIu32, 0, jpeg_size);
        return;
    }

    g_frame_handler(g_jpeg_buf, jpeg_size, g_frame_ctx);
}

/* ── 公开 API ── */

void channel_demux_init(transport_t *transport)
{
    g_transport = transport;
    g_line_pos  = 0;
    g_jpeg_buf  = (uint8_t *)malloc(g_jpeg_buf_size);
    if (!g_jpeg_buf) {
        ESP_LOGE(TAG, "JPEG buffer malloc failed");
    }
    ESP_LOGI(TAG, "Channel demuxer ready (line=%d jpeg_buf=%d)", 256, (int)g_jpeg_buf_size);
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

    /* 行超时: 500ms 无 '\\n' → 重置缓冲 (防止串口丢字节导致永久卡死) */
    if (g_line_pos > 0 && g_line_start_us > 0) {
        if (esp_timer_get_time() - g_line_start_us > 500000) {
            ESP_LOGW(TAG, "Line timeout, reset (%d bytes dropped)", (int)g_line_pos);
            g_line_pos = 0;
            g_line_start_us = 0;
        }
    }

    uint8_t ch;
    int n = g_transport->recv(g_transport, &ch, 1, 0);  /* 0ms = non-blocking */
    if (n != 1) return;

    if (ch == '\n') {
        if (g_line_pos == 0) { g_line_start_us = 0; return; }  /* 空行跳过 */

        if (g_line_pos > 0 && g_line_buf[g_line_pos - 1] == '\r')
            g_line_pos--;
        g_line_buf[g_line_pos] = '\0';
        g_line_pos = 0;
        g_line_start_us = 0;

        if (strncmp(g_line_buf, "FRAME:", 6) == 0) {
            handle_frame_line();
        } else if (strncmp(g_line_buf, "CTRL:", 5) == 0) {
            if (g_ctrl_handler)
                g_ctrl_handler(g_line_buf + 5, g_ctrl_ctx);
        } else {
            if (g_raw_handler)
                g_raw_handler(g_line_buf, g_raw_ctx);
        }
    } else if (g_line_pos < sizeof(g_line_buf) - 1) {
        if (g_line_pos == 0) g_line_start_us = esp_timer_get_time();
        g_line_buf[g_line_pos++] = (char)ch;
    }
    /* 行溢出: 静默丢弃，等下一个 \n */
}
