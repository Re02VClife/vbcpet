/** 显示后端内部接口
 *
 * 每个后端 (SPI / DSI) 实现此接口，display_pipeline.c 通过此接口调用。
 * 编译时通过 Kconfig 选择具体后端。
 */
#pragma once
#include <stdint.h>
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 显示参数 (Kconfig 定义, 各后端共用) ── */
#ifndef LCD_W
#define LCD_W  CONFIG_VBCPET_DISPLAY_W
#endif
#ifndef LCD_H
#define LCD_H  CONFIG_VBCPET_DISPLAY_H
#endif
#ifdef CONFIG_VBCPET_DISPLAY_FORMAT_RGB888
#define DISPLAY_PIXEL_BYTES  3
#else
#define DISPLAY_PIXEL_BYTES  2
#endif
#define BUF_SIZE  (LCD_W * LCD_H * DISPLAY_PIXEL_BYTES)

/* ── 后端必须实现的函数 ── */

/** 初始化后端硬件 (SPI 总线 + 面板 或 DSI 桥 + 面板)。
 *  返回 0 成功。 */
int display_backend_init(void);

/** 请求强制初始化后端 (绕过 CONFIG_VBCPET_SKIP_DISPLAY)。
 *  bring-up 'lcd' 命令调用: 串口触发才点屏, 开机仍保持跳过。 */
void display_backend_request_init(void);

/** 独立使能背光 (不碰 DSI)。若背光未初始化则先初始化, 再设亮度 0-100。
 *  bring-up 'bl' 命令调用: 隔离背光链路测试, 不会触发 DSI/卡死系统。 */
void display_backend_enable_backlight(int percent);

/** 释放后端硬件资源。 */
void display_backend_deinit(void);

/** 将帧缓冲刷到屏幕 (全屏刷新)。
 *  fb: RGB565 像素数据, LCD_W × LCD_H。 */
void display_backend_flush(const uint16_t *fb);

/** 获取面板句柄 (供 text_render 等直接 draw_bitmap)。 */
esp_lcd_panel_handle_t display_backend_get_panel(void);

/** 设置背光亮度 0-100 (SPI 后端无背光时为 no-op)。 */
void display_backend_set_brightness(int percent);

#ifdef __cplusplus
}
#endif
