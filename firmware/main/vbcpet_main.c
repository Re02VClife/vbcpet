/** vbcpet firmware — ESP32-P4 入口 (Wokwi Beta 仿真验证)
 *
 * 启动序列:
 *   1. NVS / 基础初始化
 *   2. WiFi STA (后续 WebRTC 用; 当前仅连 AP 验证)
 *   3. 音频 codec (ES7210 + ES8311) 初始化
 *   4. 打印协议 hello 消息样例到串口
 *
 * 在 Wokwi 中运行时，串口监视器会显示各阶段状态。
 * 本文件为 M2 固件骨架起点，后续接入 esp_webrtc + DataChannel 消息循环。
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "audio_codec.h"
#include "protocol.h"

static const char *TAG = "vbcpet";

/* ── WiFi (占位: 后续 WebRTC / C6 esp-hosted 用) ── */
#define WIFI_SSID     CONFIG_VBCPET_WIFI_SSID
#define WIFI_PASS     CONFIG_VBCPET_WIFI_PASSWORD

static void _wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        ESP_LOGI(TAG, "WiFi 已连接, IP: " IPSTR, IP2STR(&((ip_event_got_ip_t *)data)->ip_info.ip));
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t inst;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_event_handler, NULL, &inst));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, _wifi_event_handler, NULL, &inst));
    wifi_config_t wcfg = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ── 主任务: 初始化 + 协议样例 ── */
void app_main(void) {
    ESP_LOGI(TAG, "vbcpet firmware booting on %s (IDF %s)", CONFIG_IDF_TARGET, esp_get_idf_version());

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* WiFi (连接后 WebRTC 用) */
    wifi_init();

    /* 音频 codec (小智方案 ES7210 + ES8311) */
    ESP_ERROR_CHECK(audio_codec_init());

    /* 协议 hello 消息样例 */
    const char *caps[] = {"h264_dec", "opus", "aec"};
    char *hello = vbc_build_hello("sim-0.1.0", "wokwi-p4-preview", caps, 3);
    ESP_LOGI(TAG, "构造 hello: %s", hello);
    free(hello);

    /* button 消息样例 */
    char *btn = vbc_build_button(VBC_KEY_YES, "req_1");
    ESP_LOGI(TAG, "构造 button: %s", btn);
    free(btn);

    ESP_LOGI(TAG, "初始化完成。等待 WebRTC 连接接入…");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
