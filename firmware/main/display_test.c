/** ST7789V SPI 显示测试 — WT9932P4-TINY bring-up
 *
 * 硬件: WT9932P4-TINY + ST7789V 320x240 TFT
 * 协议: SPI (Mode 0), 16-bit RGB565
 *
 * -------- 接线说明 --------
 * TFT 引脚    →  P4 GPIO
 *   SCLK      →  GPIO17  (SPI2 CLK)
 *   MOSI/SDA  →  GPIO18  (SPI2 MOSI)
 *   CS        →  GPIO16
 *   DC        →  GPIO19
 *   RST       →  GPIO20
 *   BL        →  GPIO21  (PWM, 不接可设为 -1)
 *   VCC       →  3.3V
 *   GND       →  GND
 * --------------------------
 *
 * 依赖: ESP-IDF v5.5+ esp_lcd 组件 (内建, 不需要额外安装).
 */
#include "display_test.h"

#include <stdio.h>
#include <string.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

// ── 引脚配置 ──
// TFT → P4: CS=16, DC=17, RST=18, SDA(MOSI)=19, SCK(SCLK)=20
#define PIN_CS     GPIO_NUM_16
#define PIN_DC     GPIO_NUM_17
#define PIN_RST    GPIO_NUM_18
#define PIN_MOSI   GPIO_NUM_19
#define PIN_SCLK   GPIO_NUM_20
#define PIN_BL     -1             // 不接背光

#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// ── 全局句柄 ──
static esp_lcd_panel_handle_t  g_panel  = NULL;
static esp_lcd_panel_io_handle_t g_io   = NULL;
static spi_device_handle_t       g_spi   = NULL;

// ── 背光 PWM ──
static void backlight_init(void)
{
#if PIN_BL >= 0
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 200,  // ~78%
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
    ESP_LOGI(TAG, "背光 PWM: GPIO%d", PIN_BL);
#endif
}

static void backlight_set(uint8_t pct)
{
#if PIN_BL >= 0
    if (pct > 100) pct = 100;
    uint32_t duty = (uint32_t)pct * 255 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

// ── 绘制测试画面 ──
static void draw_test_screen(esp_lcd_panel_handle_t panel)
{
    // 画布: 320 x 240，每个像素 2 字节 (RGB565)
    const int w = LCD_WIDTH, h = LCD_HEIGHT;
    uint16_t *buf = malloc(w * h * 2);
    if (!buf) { ESP_LOGE(TAG, "malloc 画布失败"); return; }

    // 色条: 上半屏 5 列 (红/绿/蓝/白/黑)
    int sw = w / 5;
    uint16_t colors[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    for (int y = 0; y < h/2; y++) {
        for (int c = 0; c < 5; c++) {
            for (int x = c * sw; x < (c+1) * sw && x < w; x++) {
                buf[y * w + x] = colors[c];
            }
        }
    }

    // 下半屏: 文本位图 "vbcpet P4 OK" (简单点阵，8x16 字体)
    // 把下半屏清为深灰背景
    for (int y = h/2; y < h; y++) {
        for (int x = 0; x < w; x++) {
            buf[y * w + x] = 0x2104;  // 深灰
        }
    }

    // 简易 8x16 位图字体 — "vbcpet P4 OK"
    const char *msg = "vbcpet P4 OK";
    // 每个字符 8px 宽，16px 高，从 0x20 (空格) 开始
    static const uint8_t font[96][16] = {
        ['v'-0x20]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6c,0x6c,0x6c,0x6c,0x6c,0x7c,0x38,0x10},
        ['b'-0x20]={0x60,0x60,0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x7c,0x60,0x60,0x60},
        ['c'-0x20]={0x00,0x00,0x00,0x00,0x3c,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x3c,0x00,0x00,0x00},
        ['p'-0x20]={0x00,0x00,0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x7c,0x60,0x60,0x60},
        ['e'-0x20]={0x00,0x00,0x00,0x00,0x3c,0x66,0x66,0x7e,0x60,0x60,0x66,0x66,0x3c,0x00,0x00,0x00},
        ['t'-0x20]={0x00,0x00,0x10,0x18,0x10,0x10,0x10,0x7c,0x10,0x10,0x10,0x18,0x0c,0x00,0x00,0x00},
        [' '-0x20]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        ['P'-0x20]={0x00,0x00,0x00,0x00,0x78,0x64,0x64,0x64,0x64,0x64,0x7c,0x60,0x60,0x60,0x00,0x00},
        ['4'-0x20]={0x00,0x00,0x00,0x00,0x18,0x18,0x28,0x28,0x48,0x48,0xfc,0x08,0x08,0x08,0x00,0x00},
        ['O'-0x20]={0x00,0x00,0x00,0x00,0x38,0x64,0x46,0x46,0x46,0x46,0x46,0x64,0x38,0x00,0x00,0x00},
        ['K'-0x20]={0x00,0x00,0x00,0x00,0x66,0x64,0x68,0x70,0x78,0x6c,0x64,0x62,0x60,0x60,0x00,0x00},
    };

    int sx = 20, sy = h/2 + 20;
    for (int ci = 0; msg[ci]; ci++) {
        int idx = msg[ci] - 0x20;
        if (idx < 0 || idx >= 96) continue;
        for (int fy = 0; fy < 16; fy++) {
            uint8_t row = font[idx][fy];
            for (int fx = 0; fx < 8; fx++) {
                if (row & (0x80 >> fx)) {
                    int px = sx + ci*10 + fx, py = sy + fy;
                    if (px < w && py < h) buf[py * w + px] = 0xFFFF;  // 白色
                }
            }
        }
    }

    // 一次刷入全屏
    esp_lcd_panel_draw_bitmap(panel, 0, 0, w, h, buf);
    free(buf);
    ESP_LOGI(TAG, "测试画面已绘制: %s", msg);
}

// ── 公开 API ──
int display_test_start(void)
{
    ESP_LOGI(TAG, "初始化 ST7789V SPI 显示 (320x240)...");

    // 1. SPI 总线初始化
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,                // 不接
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 2. 面板 IO 配置
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = PIN_CS,
        .dc_gpio_num       = PIN_DC,
        .spi_mode          = 0,               // Mode 0 (Mode 3 命令不生效)
        .pclk_hz           = 40 * 1000 * 1000, // 10MHz (先稳再调快)
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .cs_ena_pretrans   = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &g_io));
    ESP_LOGI(TAG, "SPI2 初始化完成 SCLK=%d MOSI=%d", PIN_SCLK, PIN_MOSI);

    // 3. ST7789V 面板配置
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,  // 颜色顺序 (绿色不是色序问题，还原)
        .bits_per_pixel = 16,                          // RGB565
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(g_io, &panel_cfg, &g_panel));

    // 4. 复位 + 初始化 — 回退到最简配置
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));

    // 5. 显示方向 (关屏写确保生效)
    esp_lcd_panel_disp_on_off(g_panel, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(g_io, 0x36, (uint8_t[]){0x00}, 1);  // 竖屏
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_disp_on_off(g_panel, true);

    // 6. 背光
    backlight_init();
    backlight_set(80);
    ESP_LOGI(TAG, "ST7789V 初始化完成");

    // 7. 画测试画面
    draw_test_screen(g_panel);

    return 0;
}

esp_lcd_panel_handle_t display_test_get_panel(void)
{
    return g_panel;
}

void display_test_stop(void)
{
    if (g_panel) {
        esp_lcd_panel_disp_on_off(g_panel, false);
        esp_lcd_panel_del(g_panel);
        g_panel = NULL;
    }
    if (g_io) {
        esp_lcd_panel_io_del(g_io);
        g_io = NULL;
    }
    if (g_spi) {
        spi_bus_remove_device(g_spi);
        g_spi = NULL;
    }
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "显示已关闭");
}
