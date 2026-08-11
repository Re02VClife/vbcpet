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
