/** MIPI DSI 显示后端 — 占位 (鱼鹰 ESP32-P4C6-Core 到手后实现)
 *
 * 预期: 480×800 MIPI DSI LCD, RGB888/RGB666, esp_lcd_mipi_dsi 框架。
 */
#include "display_backend.h"
#include "esp_log.h"

static const char *TAG = "disp-dsi";

#ifndef LCD_W
#undef  LCD_W
#define LCD_W  480
#endif
#ifndef LCD_H
#undef  LCD_H
#define LCD_H  800
#endif

static esp_lcd_panel_handle_t g_panel = NULL;

int display_backend_init(void)
{
    ESP_LOGW(TAG, "MIPI DSI backend NOT IMPLEMENTED — 占位");
    // TODO: 实现 MIPI DSI 初始化 (鱼鹰板子到手)
    // 1. MIPI DSI 总线初始化
    // 2. 面板 IO + 面板创建
    // 3. 初始化序列
    return -1;
}

void display_backend_deinit(void)
{
    // TODO
}

void display_backend_flush(const uint16_t *fb)
{
    (void)fb;
    // TODO: esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_W, LCD_H, fb);
}

esp_lcd_panel_handle_t display_backend_get_panel(void)
{
    return g_panel;
}
