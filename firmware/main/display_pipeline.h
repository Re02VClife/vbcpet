/** 显示管线 — SPI 面板 + PPA JPEG 解码 + 双缓冲
 *
 * 拥有完整的显示栈：
 *   - SPI2 总线 + ST7789V 面板 (240×320 竖屏, 40MHz, MADCTL=0x00)
 *   - PPA 硬件 JPEG 解码器 + 双缓冲 (交替解码/DMA刷屏)
 *   - 启动画面渲染
 *
 * 对外只暴露 feed_jpeg() 和 get_panel()，隐藏所有硬件细节。
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化显示管线 (SPI + panel + JPEG decoder + 双缓冲)
 *  返回 0 成功，非 0 失败。 */
int display_pipeline_init(void);

/** 释放显示管线所有资源 */
void display_pipeline_deinit(void);

/** 解码并显示一帧 JPEG。内部自动选择缓冲、解码、刷屏。
 *  返回 0 成功。解码失败或尺寸超限返回非 0。 */
int display_pipeline_feed_jpeg(const uint8_t *data, size_t len);

/** 获取面板句柄 — 供 text_render 等模块直接绘制位图 */
esp_lcd_panel_handle_t display_pipeline_get_panel(void);

/** 获取共享帧缓冲 — 153600 字节 (240×320×2 RGB565)。
 *  供 text_render 等模块临时使用，用完即归还。
 *  注意: JPEG 解码期间此缓冲被占用，调用者应在非解码时使用。 */
uint16_t *display_pipeline_get_fb(void);

/** 显示内嵌的启动画面 (JPEG) */
void display_pipeline_draw_boot_screen(const uint8_t *jpeg_data, size_t len);

/** 将当前帧缓冲刷到屏幕 (与 feed_jpeg 搭配: 解码不自动刷屏,
 *  由调用者在文字叠加后统一刷新, 避免 SPI 总线并发冲突) */
void display_pipeline_flush(void);

#ifdef __cplusplus
}
#endif
