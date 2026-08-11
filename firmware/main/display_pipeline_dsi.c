/** MIPI DSI 显示后端 — 最终板 (鱼鹰 ESP32-P4C6-Core + YDP430BT009-V1)
 *
 * 屏: 4.3" 480×800 ST7102 MIPI-DSI 2-lane + I2C 触摸 (触摸驱动见 peripherals/touch.c)
 * 驱动: kevincoooool/esp_lcd_st7102 (社区组件, 含 ST7102 init 序列)
 * 引脚:
 *   LCD_RST = GPIO20 → 屏 RSTB
 *   BL_PWM  = GPIO48 → MT3608 EN (背光 PWM 调光)
 * 帧缓冲: RGB565 16bpp, 480×800 = 768KB, PSRAM (display_pipeline.c 分配)
 * 刷屏: 单缓冲解码→文字叠加→draw_bitmap 全屏推入 DPI 面板内部 FB
 *
 * ⚠️ bring-up 待核实: 模组侧 DSI lane 顺序 (D1/CLK/D0) 与屏 FPC 交叉,
 *   本后端按 lane0/1 标准顺序配置。若实板需 lane 交换/极性翻转,
 *   需在 ST7102 init_cmds 里加对应寄存器 (见 esp_lcd_st7102 vendor_config 覆盖)。
 */
#include "display_backend.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7102.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "disp-dsi";

/* ── 引脚 ── */
#define PIN_LCD_RST   GPIO_NUM_20
#define PIN_BL_PWM    GPIO_NUM_48
#define BL_BRIGHTNESS (10 * 1023 / 10)   /* 默认背光 100% (bring-up 先看全亮) */

/* ── 静态句柄 ── */
static esp_lcd_dsi_bus_handle_t    g_bus   = NULL;
static esp_lcd_panel_io_handle_t   g_io    = NULL;
static esp_lcd_panel_handle_t      g_panel = NULL;

/* ── 背光: LEDC PWM → MT3608 EN ── */
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_BL_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = BL_BRIGHTNESS,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_LOGI(TAG, "Backlight PWM GPIO%d duty=%d/1023", PIN_BL_PWM, BL_BRIGHTNESS);
}

/** 背光亮度 0-100 (供 bring-up 命令调节) */
void display_backend_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (percent * 1023) / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

int display_backend_init(void)
{
    ESP_LOGI(TAG, "Initializing MIPI DSI backend (%dx%d)...", LCD_W, LCD_H);

    backlight_init();

    /* 1. MIPI-DSI 总线 (2 lane) */
    esp_lcd_dsi_bus_config_t bus_cfg = ST7102_MIPI_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &g_bus), "bus", "DSI bus init fail");

    /* 2. DBI IO (命令通道) */
    esp_lcd_dbi_io_config_t io_cfg = ST7102_MIPI_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(g_bus, &io_cfg, &g_io), "io", "DSI DBI IO fail");

    /* 3. DPI 面板 (视频数据通道, 480×800) */
    esp_lcd_dpi_panel_config_t dpi_cfg = ST7102_MIPI_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    st7102_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus     = g_bus,
            .dpi_config  = &dpi_cfg,
        },
        .flags = { .use_mipi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,           /* RGB565, colmod 0x55 */
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7102(g_io, &panel_cfg, &g_panel), "panel", "ST7102 panel create fail");

    /* 4. 复位 + 初始化 (含 ST7102 init 序列 + DPI 启动) */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), "panel", "reset fail");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), "panel", "init fail");

    ESP_LOGI(TAG, "MIPI DSI backend ready");
    return 0;
}

void display_backend_deinit(void)
{
    if (g_panel) { esp_lcd_panel_del(g_panel); g_panel = NULL; }
    if (g_io)    { esp_lcd_panel_io_del(g_io); g_io = NULL; }
    if (g_bus)   { esp_lcd_del_dsi_bus(g_bus); g_bus = NULL; }
}

void display_backend_flush(const uint16_t *fb)
{
    if (g_panel) {
        esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_W, LCD_H, fb);
    }
}

esp_lcd_panel_handle_t display_backend_get_panel(void)
{
    return g_panel;
}
