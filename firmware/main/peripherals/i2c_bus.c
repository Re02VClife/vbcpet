/** 共享 I2C 总线实现 — i2c_master (IDF v5.5) */
#include "i2c_bus.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "i2c-bus";
static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_bus) return ESP_OK;   /* 幂等 */

    i2c_master_bus_config_t cfg = {
        .i2c_port      = -1,                  /* 自动选择 */
        .sda_io_num    = PIN_I2C_SDA,
        .scl_io_num    = PIN_I2C_SCL,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .trans_queue_depth = 8,
        .flags = { .enable_internal_pullup = true },
    };
    esp_err_t ret = i2c_new_master_bus(&cfg, &s_bus);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "bus init fail: %d", ret); return ret; }
    ESP_LOGI(TAG, "I2C bus ready (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}

esp_err_t i2c_bus_add_device(uint16_t addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_dev)
{
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_speed_hz,
    };
    return i2c_master_bus_add_device(s_bus, &dev, out_dev);
}

esp_err_t i2c_bus_probe(uint16_t addr)
{
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(s_bus, addr, 50);
}

/** 扫描整条总线的 7-bit 地址, 区分 FOUND / NACK / TIMEOUT。
 *  NACK = 该地址无设备响应 (总线正常); TIMEOUT = 事务未完成 (总线被拉死/未上拉)。 */
/* 直读法扫描: 不用 i2c_master_probe (该 API 在本平台 async i2c 下会假超时),
 * 而是按 MPU 的做法 add_device + 读 1 字节寄存器 0x00 —— 成功即 FOUND。 */
void i2c_bus_scan(void)
{
    if (!s_bus) { ESP_LOGE(TAG, "bus not initialized"); return; }
    int found = 0, nack = 0, to = 0;
    ESP_LOGI(TAG, "I2C direct-scan 0x03-0x77 (SDA=%d SCL=%d, 400k) [reg0 read]...", PIN_I2C_SDA, PIN_I2C_SCL);
    for (uint16_t a = 0x03; a <= 0x77; a++) {
        i2c_master_dev_handle_t dev = NULL;
        esp_err_t r = i2c_master_bus_add_device(s_bus,
                        &(i2c_device_config_t){ .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                                .device_address = a, .scl_speed_hz = I2C_FREQ_HZ },
                        &dev);
        if (r == ESP_OK) {
            uint8_t reg = 0x00, val = 0;
            r = i2c_master_transmit_receive(dev, &reg, 1, &val, 1, 30);
            i2c_master_bus_rm_device(dev);
        }
        if (r == ESP_OK)          { ESP_LOGI(TAG, "  0x%02X FOUND (reg0=0x%02X)", a, 0); found++; }
        else if (r == ESP_ERR_TIMEOUT) to++;
        else nack++;
    }
    ESP_LOGI(TAG, "scan done: FOUND=%d NACK=%d TIMEOUT=%d%s",
             found, nack, to, to ? "  <== TIMEOUT>0: bus held/dead or missing pullup" : "");
    if (found == 0 && to == 0) ESP_LOGW(TAG, "bus totally idle: check 4.7k pullups + device power");
}
