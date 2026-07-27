/** vbcpet firmware — ESP32-P4 AI Desktop Pet */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "transport.h"
#include "channel_demux.h"
#include "display_pipeline.h"
#include "interaction_engine.h"
#include "default_screen.h"
#include "text_render.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"

static const char *TAG = "vbcpet";

static bool g_frame_decoded = false;

static void on_frame(const uint8_t *jpeg, size_t len, void *ctx)
{ (void)ctx; if (display_pipeline_feed_jpeg(jpeg, len) == 0) g_frame_decoded = true; }

static void on_ctrl(const char *json, void *ctx)
{ (void)ctx; engine_on_ctrl_message(json); }

static void on_raw(const char *line, void *ctx)
{ (void)ctx; engine_on_raw_line(line); }

void app_main(void)
{
    ESP_LOGI(TAG, "vbcpet booting on %s (IDF %s)", CONFIG_IDF_TARGET, esp_get_idf_version());
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    transport_t *transport = transport_usb_create(16384, 16384);  /* TX=16KB 防心跳填满 */
    if (!transport) { ESP_LOGE(TAG, "Transport fail"); return; }
    if (display_pipeline_init() != 0) { ESP_LOGE(TAG, "Display fail"); return; }
    display_pipeline_draw_boot_screen(default_jpeg_data, DEFAULT_JPEG_SIZE);
    if (engine_init(transport) != 0) { ESP_LOGE(TAG, "Engine fail"); return; }
    channel_demux_init(transport);
    channel_demux_set_frame_handler(on_frame, NULL);
    channel_demux_set_ctrl_handler(on_ctrl, NULL);
    channel_demux_set_raw_handler(on_raw, NULL);
    engine_send_hello();

    /* 主循环: 底部 3 行文字 + 长文本自动滚动 */
    text_render_init(display_pipeline_get_panel());
    ESP_LOGI(TAG, "Main loop");

    /* 底部文字区域: 全宽, 3 行, scale=1, 底部留 8px 边距 */
    #define CHAT_X   2
    #define CHAT_Y   (LCD_H - 3 * 16 - 8)  /* 264 */
    #define CHAT_W   29                     /* (240-4)/8 */
    #define CHAT_H   3
    #define SCROLL_INTERVAL_US  2500000     /* 2.5 秒 */

    char *g_display_text = NULL;;

	    static int g_scroll_line = 0;
	    static int64_t g_last_scroll_us = 0;

	    while (1) {
	        channel_demux_poll();
	        char *new_text = engine_get_pending_text();
	        if (new_text) {
	            free(g_display_text);
	            g_display_text = new_text;
	            g_scroll_line = 0;
	            g_last_scroll_us = 0;
	            ESP_LOGI(TAG, "New text: %s", g_display_text);
	        }
	        if (g_frame_decoded) {
	            if (g_display_text) {
	                int total = text_render_count_lines(CHAT_W, g_display_text, 1);
	                int64_t now = esp_timer_get_time();
	                if (total > CHAT_H && g_last_scroll_us > 0 &&
	                    now - g_last_scroll_us > SCROLL_INTERVAL_US) {
	                    g_scroll_line++;
	                    if (g_scroll_line >= total) g_scroll_line = 0;
	                    g_last_scroll_us = now;
	                }
	                if (total > CHAT_H && g_last_scroll_us == 0)
	                    g_last_scroll_us = now;
	                text_render_box(CHAT_X, CHAT_Y, CHAT_W, CHAT_H,
	                                g_display_text, 0x0000, 0x0000,
	                                TEXT_ALIGN_LEFT, 1, g_scroll_line);
	            }
	            display_pipeline_flush();
	            g_frame_decoded = false;
	        }
	        engine_poll();
	        vTaskDelay(pdMS_TO_TICKS(1));
	    }
}
