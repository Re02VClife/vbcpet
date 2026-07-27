/** 交互引擎实现 — 消息分发表 + 心跳 + 用户输入回传 */
#include "interaction_engine.h"
#include "protocol.h"
#include "display_pipeline.h"
#include "text_render.h"
#include "text_input.h"
#include "default_screen.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "engine";

static transport_t *g_transport = NULL;
static int64_t      g_last_heartbeat = 0;
static bool         g_hello_sent = false;
static char        *g_pending_text = NULL;  /* 待主循环渲染的文字 */

/* ── 转发: 发送 CTRL 前缀 JSON ── */
static void send_ctrl(const char *json)
{
    if (!g_transport || !json) return;

    char prefix[] = "CTRL:";
    int  plen = (int)strlen(prefix);
    int  jlen = (int)strlen(json);
    int  total = plen + jlen + 1;
    char *buf = (char *)malloc(total);
    if (!buf) return;

    memcpy(buf, prefix, plen);
    memcpy(buf + plen, json, jlen);
    buf[total - 1] = '\n';

    g_transport->send(g_transport, (uint8_t *)buf, total, 50);
    free(buf);
}

/* 非阻塞发送 — 心跳专用, 不阻塞主循环 (防 USB RX 堆积) */
static void send_ctrl_nb(const char *json)
{
    if (!g_transport || !json) return;

    char prefix[] = "CTRL:";
    int  plen = (int)strlen(prefix);
    int  jlen = (int)strlen(json);
    int  total = plen + jlen + 1;
    char *buf = (char *)malloc(total);
    if (!buf) return;

    memcpy(buf, prefix, plen);
    memcpy(buf + plen, json, jlen);
    buf[total - 1] = '\n';

    g_transport->send(g_transport, (uint8_t *)buf, total, 0);  /* timeout=0 */
    free(buf);
}

/* ── 用户文本回调: 文本行就绪 → 构建 user_text 发送 ── */
static void on_user_text_line(const char *line, void *ctx)
{
    (void)ctx;
    char *json = vbc_build_user_text(line);
    if (json) {
        ESP_LOGI(TAG, "User text: %s", line);
        send_ctrl(json);
        free(json);
    }
}

/* ── 消息分发表 ── */
void engine_on_ctrl_message(const char *json)
{
    if (!json) { return; }

    const char *type = vbc_parse_type(json);
    if (!type) {
        vbc_char_event_t evt;
        if (vbc_decode_event(json, &evt) == 0) {
            type = evt.type;
        } else {
            ESP_LOGW(TAG, "Unparseable ctrl: %.80s", json);
            return;
        }
    }

    ESP_LOGI(TAG, "Ctrl: %s", type);

    /* ── say / speech: 传递但暂不渲染 (排查用) ── */
    if (strcmp(type, VBC_T_SAY) == 0 || strcmp(type, VBC_EVT_SPEECH) == 0) {
        char *text = NULL;
        if (!vbc_parse_say(json, &text) || !text) { return; }
        ESP_LOGI(TAG, "Say: %s", text);
        free(g_pending_text);
        g_pending_text = text;
        return;
    }

    /* ── panel: 全屏纯红测试 ── */
    if (strcmp(type, VBC_T_PANEL) == 0) {
        vbc_tokens_t tokens = {0};
        double cost = 0;
        if (!vbc_parse_panel(json, &tokens, &cost)) { return; }
        uint16_t *fb = display_pipeline_get_fb();
        esp_lcd_panel_handle_t panel = display_pipeline_get_panel();
        /* 面板数据仅日志记录，暂不绘制 */
        return;
    }

    /* ── work_status ── */
    if (strcmp(type, VBC_T_WORK_STATUS) == 0) {
        char *state = NULL, *detail = NULL;
        if (vbc_parse_work_status(json, &state, &detail)) {
            ESP_LOGI(TAG, "Work: %s — %s", state ? state : "?", detail ? detail : "");
        }
        free(state);
        free(detail);
        return;
    }

    /* ── prompt_confirm / permission ── */
    if (strcmp(type, VBC_T_PROMPT_CONFIRM) == 0) {
        char *id = NULL, *reason = NULL;
        if (vbc_parse_prompt_confirm(json, &id, &reason) && reason) {
            ESP_LOGI(TAG, "Permission: id=%s reason=%s", id ? id : "?", reason);
            esp_lcd_panel_handle_t panel = display_pipeline_get_panel();
            if (panel) {
                text_render_box(0, LCD_H / 2, 30, 3, reason, 0xFFFF, 0xF800, TEXT_ALIGN_CENTER, 1, 0);
            }
        }
        free(id);
        free(reason);
        return;
    }

    /* ── heartbeat (from PC) — 忽略 ── */
    if (strcmp(type, VBC_T_HEARTBEAT) == 0) { return; }

    ESP_LOGD(TAG, "Unhandled msg type: %s", type);
}

/* ── 原始文本行 → 喂入 text_input ── */
void engine_on_raw_line(const char *line)
{
    if (!line || !line[0]) return;
    text_input_feed_line(line);
}

/* ── 周期轮询 ── */
void engine_poll(void)
{
    /* 心跳禁用 — USB Serial JTAG TX/RX 不可共存。
     * 存活检测由 PC 端根据推流活动判断。 */
    (void)g_last_heartbeat;
}

/* ── 初始化和 hello ── */
int engine_init(transport_t *transport)
{
    g_transport = transport;
    g_last_heartbeat = 0;

    /* 初始化子模块 */
    esp_lcd_panel_handle_t panel = display_pipeline_get_panel();
    if (panel) text_render_init(panel);

    text_input_init(on_user_text_line, NULL);

    ESP_LOGI(TAG, "Engine initialized");
    return 0;
}

void engine_send_hello(void)
{
    /* hello 禁用 — USB TX/RX 冲突。P4 存活由推流活动判断。 */
    g_hello_sent = true;
    g_last_heartbeat = esp_timer_get_time();
}

char *engine_get_pending_text(void)
{
    char *t = g_pending_text;
    g_pending_text = NULL;
    return t;
}
