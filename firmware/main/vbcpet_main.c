/** vbcpet firmware — ESP32-P4 AI Desktop Pet */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_ldo_regulator.h"
#include "power_rail.h"

#include "transport.h"
#include "channel_demux.h"
#include "display_pipeline.h"
#include "interaction_engine.h"
#include "default_screen.h"
#include "text_render.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "peripherals/sk6812.h"
#ifdef CONFIG_DISPLAY_BACKEND_DSI
#include "bringup.h"
#endif

static const char *TAG = "vbcpet";

static bool g_frame_decoded = false;

static bool s_led_init = false;

static void on_frame(const uint8_t *jpeg, size_t len, void *ctx)
{
    (void)ctx;
    /* LED 诊断: 帧到达 app 层 = 绿闪 (demux/UART RX 正常的证据) */
    if (!s_led_init) { sk6812_init(); s_led_init = true; }
    sk6812_set_all(0, 100, 0);
    sk6812_show();
    if (display_pipeline_feed_jpeg(jpeg, len) == 0) g_frame_decoded = true;
}

static void on_ctrl(const char *json, void *ctx)
{ (void)ctx; engine_on_ctrl_message(json); }

static void on_raw(const char *line, void *ctx)
{ (void)ctx; engine_on_raw_line(line); }

#ifdef CONFIG_VBCPET_MINIMAL_USJ
/** 最小 USB-Serial/JTAG 隔离测试: 只装 USJ 驱动 + SK6812 心跳, 永不返回 */
static void minimal_usj_test(void)
{
    ESP_LOGI(TAG, "[MINIMAL-USJ] boot (no host/display/bringup)");
    bool sk_ok = (sk6812_init() == 0);
    transport_t *t = transport_usb_create(16384, 16384);
    bool usj_ok = (t != NULL);
    ESP_LOGI(TAG, "[MINIMAL-USJ] sk6812=%s usj-driver=%s",
             sk_ok ? "ok" : "fail", usj_ok ? "OK" : "FAIL");

    if (sk_ok) {
        sk6812_clear();
        sk6812_set_all(usj_ok ? 0 : 255, usj_ok ? 255 : 0, 0); /* 绿=OK 红=FAIL */
        sk6812_show();
    }
    bool on = false;
    while (1) {
        if (sk_ok) {
            sk6812_set_all(on ? 0 : (usj_ok ? 0 : 255),
                           on ? 0 : (usj_ok ? 255 : 0), 0);
            sk6812_show();
        }
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
#endif /* CONFIG_VBCPET_MINIMAL_USJ */

/* ── 内部 LDO_VO4 → GPIO39~48 组 IO 供电轨 (2026-09-25 实测定案) ──────────────
 *
 * 手册 pin66 = `ESP_LDO_VO4`, 原文 "Output power supply (0.5-2.7V or 3.3V, max 0.2A)";
 * IDF `sd_pwr_ctrl_by_on_chip_ldo.h` 注释:
 *   "On-chip LDO channel ID, e.g. set to `4` is the `LDO_VO4` is connected to power the SDMMC IO"
 *
 * 实测 (tools/gp_sweep.py, 2026-09-24/25): GPIO39~48 这组 pad 的 IO 供电轨就是它,
 * 复位后停在 ~1.2V 的默认档。不配置的后果是一整组外设全瘫:
 *   SD 卡 sdmmc_card_init failed / PA_EN(GPIO46) 拉不起 / MPU_INT(GPIO47) 收不到 /
 *   BL_PWM(GPIO48) 驱不动 MT3608 EN。
 *
 * 2026-09-25 实测三档 (拆掉 pin66 上的外部连线后): ldo 4 1800 / 2700 / 3300
 *   ⇒ pin66 与 GPIO48 同步跟随 1.8 / 2.7 / 3.3V ⇒ 通道号 4 正确, 软件完全可控。
 * 注意 3300 走的是 "rail bypass" (直通输入轨, IDF `LDO_LL_RAIL_VOLTAGE_MV`),
 * 1800/2700 才是片内 LDO 调压档 —— 两档都能用。
 *
 * ⚠️ IDF 明确约束: if the LDO channel is adjustable, you cannot acquire it multiple times。
 * 所以这一条轨在这里统一 acquire 一次, 其它任何地方都不要再去 acquire 通道 4。
 */
/* 全局: 控制台 `ldo 4 <mv>` (bringup.c) 要复用它调压 —— adjustable 通道不能 acquire 两次 */
esp_ldo_channel_handle_t g_ldo_vo4 = NULL;

static void ldo_vo4_init(void)
{
#if CONFIG_VBCPET_LDO_VO4_MV > 0
    esp_ldo_channel_config_t cfg = {
        .chan_id          = 4,
        .voltage_mv       = CONFIG_VBCPET_LDO_VO4_MV,
        .flags.adjustable = true,
    };
    esp_err_t err = esp_ldo_acquire_channel(&cfg, &g_ldo_vo4);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LDO_VO4 (GPIO39~48 IO rail) -> %d mV: OK", CONFIG_VBCPET_LDO_VO4_MV);
    } else {
        ESP_LOGE(TAG, "LDO_VO4 acquire @ %d mV failed: %s",
                 CONFIG_VBCPET_LDO_VO4_MV, esp_err_to_name(err));
    }
#else
    ESP_LOGW(TAG, "LDO_VO4 未配置 (CONFIG_VBCPET_LDO_VO4_MV=0): GPIO39~48 组停在 ~1.2V");
#endif
}

void app_main(void)
{
#ifdef CONFIG_VBCPET_MINIMAL_USJ
    minimal_usj_test();   /* 不返回: 隔离测试模式 */
#endif
    ESP_LOGI(TAG, "vbcpet booting on %s (IDF %s)", CONFIG_IDF_TARGET, esp_get_idf_version());

    /* IO 供电轨必须最先配好: 显示(背光 GPIO48)/SD/音频(PA_EN GPIO46)/IMU(INT GPIO47) 全靠它 */
    ldo_vo4_init();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    /* RX 32KB: 480×800 JPEG 每帧 40-80KB, 大缓冲防 USB 丢帧 */
#ifdef CONFIG_VBCPET_TRANSPORT_UART
    /* V1 推流: UART0/CH340 @2M 复用链路 (console=NONE, RX 64KB 抗帧抖动) */
    transport_t *transport = transport_uart_create(65536, 16384);
#else
    transport_t *transport = transport_usb_create(32768, 16384);  /* TX=16KB 防心跳填满 */
#endif
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

#ifdef CONFIG_DISPLAY_BACKEND_DSI
    /* 最终板: 外设 bring-up + 控制台命令 */
    bringup_init(transport);
#ifndef CONFIG_VBCPET_TRANSPORT_UART
    bringup_start_console();   /* UART 复用推流时让位: console 任务读 UART0 会抢帧字节 */
#endif
#endif

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
