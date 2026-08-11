/** MPU-6500 六轴 (I2C 0x68, INT GPIO47) — 拿起/摇晃/倾斜检测 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 (共享 I2C 总线, 需先 i2c_bus_init)。返回 0 成功。 */
int mpu6500_init(void);

/** 读取 accel (±2g) 与 gyro (±250dps) 原始值。 */
void mpu6500_read_raw(int16_t *accel_xyz, int16_t *gyro_xyz);

/** 自检: 校验 WHO_AM_I + 打印 accel/gyro 10 次。返回 0=通过。 */
int mpu6500_self_test(void);

#ifdef __cplusplus
}
#endif
