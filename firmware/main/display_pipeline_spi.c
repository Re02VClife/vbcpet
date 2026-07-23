/** SPI ST7789V 显示后端 — WT9932P4-TINY 已验证
 *
 * 引脚: CS=16 DC=17 RST=18 MOSI=19 SCLK=20
 * 协议: SPI Mode 0, 40MHz, RGB565, 240×320 竖屏 (MADCTL=0x00)
 */
#include "display_backend.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/gpio.h"

static const char *TAG = "disp-spi";

/* ── 引脚 ── */
#define PIN_CS     GPIO_NUM_16
#define PIN_DC     GPIO_NUM_17
#define PIN_RST    GPIO_NUM_18
#define PIN_MOSI   GPIO_NUM_19
#define PIN_SCLK   GPIO_NUM_20

/* ── 静态句柄 ── */
static esp_lcd_panel_handle_t    g_panel = NULL;
static esp_lcd_panel_io_handle_t g_io    = NULL;

int display_backend_init(void)
{
    ESP_LOGI(TAG, "Initializing SPI ST7789V backend (%dx%d, 40MHz)...", LCD_W, LCD_H);

    /* 1. SPI2 总线 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = BUF_SIZE,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "SPI2 bus init fail"); return -1; }

    /* 2. 面板 IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = PIN_CS,
        .dc_gpio_num       = PIN_DC,
        .spi_mode          = 0,
        .pclk_hz           = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .cs_ena_pretrans   = 1,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &g_io);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Panel IO create fail"); return -1; }

    /* 3. ST7789V 面板 */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ret = esp_lcd_new_panel_st7789(g_io, &panel_cfg, &g_panel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Panel create fail"); return -1; }

    /* 4. 初始化 + 竖屏 */
    esp_lcd_panel_reset(g_panel);
    esp_lcd_panel_init(g_panel);
    esp_lcd_panel_disp_on_off(g_panel, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(g_io, 0x36, (uint8_t[]){0x00}, 1); /* MADCTL=0x00 */
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_disp_on_off(g_panel, true);

    ESP_LOGI(TAG, "SPI backend ready");
    return 0;
}

void display_backend_deinit(void)
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
    spi_bus_free(SPI2_HOST);
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
