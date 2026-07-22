/** ST7789V SPI 显示测试 — WT9932P4-TINY bring-up
 *
 * 使用 ESP-IDF esp_lcd 驱动 SPI ST7789V 面板 (320x240)。
 * 接线: SCLK/MOSI/CS/DC/RST/BL 接对应 GPIO，VCC=3.3V, GND=GND。
 * 所有引脚在 menuconfig 或下方宏定义中配置。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 SPI 总线 + ST7789V 面板，返回 0 成功。
 *  调用后屏幕显示 vbcpet 测试画面（色条 + 文字）。 */
int display_test_start(void);

/** 反初始化，释放资源。 */
void display_test_stop(void);

/** 获取 panel 句柄 (供外部调用 draw_bitmap 等) */
#include "esp_lcd_types.h"
esp_lcd_panel_handle_t display_test_get_panel(void);

#ifdef __cplusplus
}
#endif
