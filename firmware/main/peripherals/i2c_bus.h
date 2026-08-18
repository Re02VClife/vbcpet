/** 共享 I2C 总线 (触摸 / NFC / MPU-6500 / OV5640 共用 GPIO8/21) */
#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2C 总线 (幂等)。返回 ESP_OK。 */
esp_err_t i2c_bus_init(void);

/** 获取总线句柄 (未初始化返回 NULL)。 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

/** 在共享总线上注册一个设备。 */
esp_err_t i2c_bus_add_device(uint16_t addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out_dev);

/** 探测某 7-bit 地址是否存在 (0=存在)。 */
esp_err_t i2c_bus_probe(uint16_t addr);

/** 扫描全总线 (0x03-0x77), 打印 FOUND/NACK/TIMEOUT 分类。 */
void i2c_bus_scan(void);

#ifdef __cplusplus
}
#endif
