/** NFC NT3H2111 驱动实现 */
#include "nfc.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "nfc";

static i2c_master_dev_handle_t s_dev = NULL;

int nfc_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus init fail");
    ESP_RETURN_ON_ERROR(i2c_bus_add_device(NFC_I2C_ADDR, 400000, &s_dev), TAG, "i2c add device fail");

    gpio_config_t fd = {
        .pin_bit_mask = 1ULL << PIN_NFC_FD,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* FD 开漏, 板上 4.7k 上拉 */
    };
    gpio_config(&fd);

    ESP_LOGI(TAG, "NT3H2111 ready (I2C 0x%02X, FD GPIO%d)", NFC_I2C_ADDR, PIN_NFC_FD);
    return 0;
}

int nfc_read_user(uint8_t offset, uint8_t *buf, size_t len)
{
    if (!s_dev) return -1;
    if (i2c_master_transmit_receive(s_dev, &offset, 1, buf, len, 100) != ESP_OK) return -1;
    return 0;
}

bool nfc_field_present(void)
{
    return gpio_get_level(PIN_NFC_FD) == 0;   /* FD 低=场存在 */
}

int nfc_self_test(void)
{
    if (i2c_bus_probe(NFC_I2C_ADDR) != ESP_OK) {
        ESP_LOGE(TAG, "I2C probe 0x%02X FAIL", NFC_I2C_ADDR);
        return -1;
    }
    uint8_t buf[16];
    if (nfc_read_user(0x00, buf, sizeof(buf)) != 0) {
        ESP_LOGE(TAG, "user memory read FAIL");
        return -2;
    }
    ESP_LOGI(TAG, "User mem[0:15]: %02X %02X %02X %02X | %02X %02X %02X %02X | ...",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    ESP_LOGI(TAG, "FD GPIO%d = %d (1=无场 0=场存在), 请用手机贴一下试试",
             PIN_NFC_FD, gpio_get_level(PIN_NFC_FD));
    for (int i = 0; i < 20; i++) {
        if (nfc_field_present()) ESP_LOGI(TAG, "  → NFC 场检测到!");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
