/** vbcpet firmware — ESP32-P4 Phase 0 Bring-up */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "serial_display.h"

static const char *TAG = "vbcpet";

void app_main(void)
{
    ESP_LOGI(TAG, "vbcpet firmware booting on %s (IDF %s)", CONFIG_IDF_TARGET, esp_get_idf_version());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS OK");

    // USB Serial JTAG 专用于数据, console 走 UART0
    ESP_LOGI(TAG, "启动串口显示模式...");
    serial_display_start();
}
