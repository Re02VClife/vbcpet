/** vbcpet firmware — ESP32-P4 AI Desktop Pet */
#include <stdio.h>
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
#include "driver/usb_serial_jtag.h"

static const char *TAG = "vbcpet";

static void on_frame(const uint8_t *jpeg, size_t len, void *ctx)
{ (void)ctx; display_pipeline_feed_jpeg(jpeg, len); }

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
    transport_t *transport = transport_usb_create(16384, 4096);
    if (!transport) { ESP_LOGE(TAG, "Transport fail"); return; }
    if (display_pipeline_init() != 0) { ESP_LOGE(TAG, "Display fail"); return; }
    display_pipeline_draw_boot_screen(default_jpeg_data, DEFAULT_JPEG_SIZE);
    if (engine_init(transport) != 0) { ESP_LOGE(TAG, "Engine fail"); return; }
    channel_demux_init(transport);
    channel_demux_set_frame_handler(on_frame, NULL);
    channel_demux_set_ctrl_handler(on_ctrl, NULL);
    channel_demux_set_raw_handler(on_raw, NULL);
    engine_send_hello();

    /* 主循环: 文字持久渲染 (每帧重绘以对抗覆盖) */
    text_render_init(display_pipeline_get_panel());
    ESP_LOGI(TAG, "Main loop");
    char *g_display_text = NULL;  /* 持久存储当前显示文字 */
    while (1) {
        /* 1. 检查新的 engine 文字 */
        char *new_text = engine_get_pending_text();
        if (new_text) {
            free(g_display_text);
            g_display_text = new_text;
            ESP_LOGI(TAG, "New text: %s", g_display_text);
        }
        /* 2. 每帧重绘 */
        if (g_display_text) {
            text_render_box(2, 60, 14, 6, g_display_text, 0x07E0, 0x0000, TEXT_ALIGN_LEFT, 2);
        }
        /* 3. demux 收发 */
        channel_demux_poll();
        /* 4. engine 周期性任务 (心跳等) */
        engine_poll();
        /* 5. 让出 CPU — 1ms tick → 理论最大 1000 poll/s */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
