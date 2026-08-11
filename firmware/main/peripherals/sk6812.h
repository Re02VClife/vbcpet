/** SK6812 ×4 (WS2812B 协议, GRB) — RMT 实现, 5V 供电 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 RMT TX 通道 (GPIO51)。返回 0 成功。 */
int sk6812_init(void);

/** 设置某灯颜色 (0-3), r/g/b 0-255。show() 后生效。 */
void sk6812_set_color(int index, uint8_t r, uint8_t g, uint8_t b);

/** 全部灯设同色。 */
void sk6812_set_all(uint8_t r, uint8_t g, uint8_t b);

/** 刷新到 LED (发送一帧)。 */
void sk6812_show(void);

/** 熄灭全部。 */
void sk6812_clear(void);

/** 自检: 逐个灯红/绿/蓝循环。返回 0=通过。 */
int sk6812_self_test(void);

#ifdef __cplusplus
}
#endif
