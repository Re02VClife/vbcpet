/** 触摸 (ST7123 TDDI, 地址待 bring-up 探测确认) — 基于共享 I2C
 *
 * ⚠️ 重要: ST7123 触摸引擎由 LCD_RST (GPIO20) 控制, TP_RST (GPIO9)
 *   不应被 toggle (会打掉触摸 I2C)。本驱动不做 TP_RST 复位。
 *   默认地址 0x55 与 NFC (0x55) 冲突, 实际地址以自检探测结果为准。
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化: 探测触摸控制器地址。返回 0=找到设备。 */
int touch_init(void);

/** 当前触摸控制器 I2C 地址 (0 = 未找到)。 */
uint8_t touch_get_addr(void);

/** 读取触摸点 (若支持)。返回 0=有触点, 1=无触点, <0=错误。 */
int touch_read(int *x, int *y, int *id);

/** 自检: 探测常用地址 + 轮询触摸 10s (请点屏幕)。返回 0=通过。 */
int touch_self_test(void);

#ifdef __cplusplus
}
#endif
