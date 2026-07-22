/** 显示管线实现 — SPI ST7789V + PPA JPEG 解码 + 双缓冲
 *
 * 硬件: WT9932P4-TINY + ST7789V 240×320 TFT
 * 协议: SPI Mode 0, 16-bit RGB565, 40MHz
 *
 * 从 display_test.c (SPI/面板初始化) 和 vbcpet_main.c (JPEG解码/双缓冲) 提取整合。
 */

#include "display_pipeline.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/jpeg_decode.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

static const char *TAG = "disp";

/* ── 引脚 (WT9932P4-TINY 已验证) ── */
#define PIN_CS     GPIO_NUM_16
#define PIN_DC     GPIO_NUM_17
#define PIN_RST    GPIO_NUM_18
#define PIN_MOSI   GPIO_NUM_19
#define PIN_SCLK   GPIO_NUM_20

#define LCD_W      240
#define LCD_H      320
#define BUF_SIZE   (LCD_W * LCD_H * 2)  /* 153,600 bytes RGB565 */

/* ── 静态句柄 ── */
static esp_lcd_panel_handle_t    g_panel = NULL;
static esp_lcd_panel_io_handle_t g_io    = NULL;
static jpeg_decoder_handle_t     g_jpeg  = NULL;

/* ── 双缓冲 ── */
static uint8_t *g_outbuf[2] = {NULL, NULL};
static int      g_buf_idx   = 0;

/* ── 公开 API ── */

int display_pipeline_init(void)
{
    ESP_LOGI(TAG, "初始化 ST7789V SPI 显示 (%dx%d) + PPA JPEG 解码器 ...", LCD_W, LCD_H);

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

    /* 4. 复位 + 初始化 + 竖屏 */
    esp_lcd_panel_reset(g_panel);
    esp_lcd_panel_init(g_panel);
    esp_lcd_panel_disp_on_off(g_panel, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(g_io, 0x36, (uint8_t[]){0x00}, 1); /* MADCTL=0x00 竖屏 */
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_disp_on_off(g_panel, true);

    /* 5. PPA JPEG 解码器 */
    jpeg_decode_engine_cfg_t eng_cfg = { .timeout_ms = 2000 };
    ret = jpeg_new_decoder_engine(&eng_cfg, &g_jpeg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "JPEG decoder init fail"); return -1; }

    /* 6. 双缓冲 (DMA 对齐) */
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t actual = 0;
    g_outbuf[0] = jpeg_alloc_decoder_mem(BUF_SIZE, &mem_cfg, &actual);
    g_outbuf[1] = jpeg_alloc_decoder_mem(BUF_SIZE, &mem_cfg, &actual);
    if (!g_outbuf[0]) g_outbuf[0] = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_outbuf[1]) g_outbuf[1] = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_outbuf[0] || !g_outbuf[1]) { ESP_LOGE(TAG, "Dual buffer alloc fail"); return -1; }

    ESP_LOGI(TAG, "Display pipeline ready (SPI2 %dMHz, MADCTL=0x00, JPEG PPA, 双缓冲)",
             40);
    return 0;
}

void display_pipeline_deinit(void)
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
    if (g_jpeg) {
        jpeg_del_decoder_engine(g_jpeg);
        g_jpeg = NULL;
    }
    free(g_outbuf[0]); g_outbuf[0] = NULL;
    free(g_outbuf[1]); g_outbuf[1] = NULL;
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "Display pipeline deinitialized");
}

int display_pipeline_feed_jpeg(const uint8_t *data, size_t len)
{
    if (!g_panel || !g_jpeg || !g_outbuf[0]) return -1;

    /* 获取图片信息 */
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(data, len, &info) != ESP_OK) return -1;
    if (info.width == 0 || info.height == 0 || info.width > LCD_W || info.height > LCD_H)
        return -1;

    /* 解码配置 */
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };

    /* 双缓冲: 解码到 buf[0] 等待 SPI DMA 时下次解码到 buf[1] */
    uint32_t out_len = 0;
    if (jpeg_decoder_process(g_jpeg, &dec_cfg, (uint8_t *)data, len,
                             g_outbuf[g_buf_idx], BUF_SIZE, &out_len) != ESP_OK)
        return -1;

    /* 刷屏 */
    esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_W, LCD_H, g_outbuf[g_buf_idx]);

    g_buf_idx ^= 1;  /* 切换缓冲 */
    return 0;
}

esp_lcd_panel_handle_t display_pipeline_get_panel(void)
{
    return g_panel;
}

uint16_t *display_pipeline_get_fb(void)
{
    return (uint16_t *)g_outbuf[0];
}

void display_pipeline_draw_boot_screen(const uint8_t *jpeg_data, size_t len)
{
    if (!g_panel || !g_jpeg || !g_outbuf[0]) return;

    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(jpeg_data, len, &info) != ESP_OK) return;
    if (info.width == 0 || info.height == 0 || info.width > LCD_W || info.height > LCD_H)
        return;

    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    if (jpeg_decoder_process(g_jpeg, &dec_cfg, (uint8_t *)jpeg_data, len,
                             g_outbuf[0], BUF_SIZE, &out_len) == ESP_OK) {
        esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_W, LCD_H, g_outbuf[0]);
        ESP_LOGI(TAG, "Boot screen displayed");
    }
}
