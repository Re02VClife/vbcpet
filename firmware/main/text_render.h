/** 本地位图文字渲染 API
 *
 * 在 ST7789V (240×320) 上渲染完整的 8×16 ASCII 位图字体。
 * 支持单行渲染 (str) 和多行文本框 (box) + 自动换行。
 */
#pragma once
#include "esp_lcd_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_W  8
#define FONT_H  16
#define LCD_W   240
#define LCD_H   320

/** 文本对齐 */
typedef enum { TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER } text_align_t;

/** 绑定面板句柄 */
void text_render_init(esp_lcd_panel_handle_t panel);

/** 清屏为纯色 */
void text_render_clear(uint16_t bg_color);

/** 单行文字 — 从 (x,y) 开始，scale 1-4 倍 */
void text_render_str(int x, int y, const char *text,
                     uint16_t fg, uint16_t bg, int scale);

/** 计算文本在 w_chars 宽度下的换行总行数 */
int text_render_count_lines(int w_chars, const char *text, int scale);

/** 多行文本框 — w_chars 字符宽, h_lines 行高，自动换行。
 *  scroll_lines: 跳过前 N 行 (0=从头开始), 用于滚动效果。
 *  文本超出框高时截断。 */
void text_render_box(int x, int y, int w_chars, int h_lines,
                     const char *text, uint16_t fg, uint16_t bg,
                     text_align_t align, int scale, int scroll_lines);

#ifdef __cplusplus
}
#endif
