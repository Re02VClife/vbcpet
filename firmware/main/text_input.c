/** 文字输入 — 字符累积器实现 */
#include "text_input.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "txtin";

static text_line_callback_t g_callback = NULL;
static void                *g_ctx = NULL;

void text_input_init(text_line_callback_t callback, void *ctx)
{
    g_callback = callback;
    g_ctx      = ctx;
    ESP_LOGI(TAG, "Text input ready");
}

void text_input_feed_line(const char *line)
{
    if (!g_callback || !line || !line[0]) return;
    g_callback(line, g_ctx);
}
