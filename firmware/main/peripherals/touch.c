/** 触摸驱动实现 — ST7123 TDDI (Sitronix), 探测式 */
#include "touch.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "touch";

/* ST7123 默认 0x55 (与 NFC 冲突 → 可能改址); 常见候补 */
static const uint16_t PROBE_ADDRS[] = { 0x55, 0x24, 0x15, 0x38, 0x48, 0x5D };
#define PROBE_COUNT (sizeof(PROBE_ADDRS) / sizeof(PROBE_ADDRS[0]))

static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_addr = 0;

/* ST7123 数据寄存器 (Sitronix 协议, 参考 ST1633 类似族) */
#define ST7123_STATUS_REG 0x00
#define ST7123_TOUCH_REG  0x01

int touch_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus init fail");

    for (int i = 0; i < (int)PROBE_COUNT; i++) {
        if (i2c_bus_probe(PROBE_ADDRS[i]) == ESP_OK) {
            /* 区分触摸与 NFC: NFC 用户内存读回有 NACK/全 0x00 特征;
             * 触摸 0x00 寄存器读回状态字节。这里先记录, 具体协议带起来确认。 */
            ESP_LOGI(TAG, "I2C 0x%02X 响应 — 可能是触摸或 NFC", PROBE_ADDRS[i]);
            if (s_addr == 0) {
                if (i2c_bus_add_device(PROBE_ADDRS[i], I2C_FREQ_HZ, &s_dev) == ESP_OK) {
                    s_addr = PROBE_ADDRS[i];
                    ESP_LOGI(TAG, "Touch init @0x%02X (TP_RST GPIO%d 不复位 — ST7123 由 LCD_RST 控制)",
                             s_addr, PIN_TP_RST);
                    return 0;
                }
            }
        }
    }
    ESP_LOGW(TAG, "Touch 控制器未探测到 (地址待确认, 见 README 打样疑点)");
    return -1;
}

uint8_t touch_get_addr(void)
{
    return s_addr;
}

int touch_read(int *x, int *y, int *id)
{
    if (!s_dev) return -1;

    uint8_t status = 0;
    /* repeated-START 单事务: 写寄存器指针 + 读状态 */
    if (i2c_master_transmit_receive(s_dev, &(uint8_t){ST7123_STATUS_REG}, 1, &status, 1, 50) != ESP_OK)
        return -1;

    int points = status & 0x0F;
    if (points == 0) return 1;

    /* 读触点数据 (前 4 点, 每点 4 字节: 状态/ID/X/Y) */
    uint8_t data[16];
    if (i2c_master_transmit_receive(s_dev, &(uint8_t){ST7123_TOUCH_REG}, 1, data, sizeof(data), 50) != ESP_OK)
        return -1;

    if (x) *x = ((data[2] & 0x0F) << 8) | data[1];
    if (y) *y = ((data[4] & 0x0F) << 8) | data[3];
    if (id) *id = (data[0] >> 4) & 0x0F;
    return 0;
}

int touch_self_test(void)
{
    if (!s_dev) {
        ESP_LOGI(TAG, "Self-test: 扫描 I2C 总线找触摸控制器…");
        for (int i = 0; i < (int)PROBE_COUNT; i++) {
            esp_err_t r = i2c_bus_probe(PROBE_ADDRS[i]);
            ESP_LOGI(TAG, "  0x%02X: %s", PROBE_ADDRS[i], r == ESP_OK ? "✔ 响应" : "—");
        }
        if (touch_init() != 0) {
            ESP_LOGE(TAG, "触摸控制器未找到");
            return -1;
        }
    }
    ESP_LOGI(TAG, "Self-test: 请点屏幕 (10s)… 触摸地址=0x%02X", s_addr);
    for (int i = 0; i < 100; i++) {
        int x = 0, y = 0, id = 0;
        int r = touch_read(&x, &y, &id);
        if (r == 0) ESP_LOGI(TAG, "  touch id=%d x=%d y=%d", id, x, y);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
