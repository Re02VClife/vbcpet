/** 舵机 ×2 (SERVO_YAW GPIO24 / SERVO_PITCH GPIO25, 50Hz 500-2500µs) */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 LEDC 50Hz, 上电回中 (1500µs)。返回 0 成功。 */
int servo_init(void);

/** 设置角度 (-90..+90), index 0=YAW 1=PITCH。 */
void servo_set_angle(int index, int angle_deg);

/** 自检: 回中→±90° 扫动。返回 0=通过。 */
int servo_self_test(void);

#ifdef __cplusplus
}
#endif
