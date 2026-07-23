/** 显示管线 — JPEG 解码 + 双缓冲 + 后端抽象
 *
 * 公共逻辑 (JPEG PPA 硬解, 双缓冲管理) + 统一对外 API。
 * 具体显示硬件 (SPI ST7789V / MIPI DSI) 由 display_backend_* 实现。
 */
#include "display_pipeline.h"
#include "display_backend.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "disp";

/* ── JPEG 解码器 ── */
static jpeg_decoder_handle_t g_jpeg = NULL;

/* ── 双缓冲 ── */
static uint8_t *g_outbuf[2] = {NULL, NULL};
static int      g_buf_idx   = 0;

/* ── 公开 API ── */

int display_pipeline_init(void)
{
    ESP_LOGI(TAG, "Initializing display pipeline (%dx%d)...", LCD_W, LCD_H);

    /* 1. 后端硬件初始化 */
    if (display_backend_init() != 0) {
        ESP_LOGE(TAG, "Backend init fail");
        return -1;
    }

    /* 2. PPA JPEG 解码器 */
    jpeg_decode_engine_cfg_t eng_cfg = { .timeout_ms = 2000 };
    if (jpeg_new_decoder_engine(&eng_cfg, &g_jpeg) != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decoder init fail");
        return -1;
    }

    /* 3. 双缓冲 (DMA 对齐) */
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t actual = 0;
    g_outbuf[0] = jpeg_alloc_decoder_mem(BUF_SIZE, &mem_cfg, &actual);
    g_outbuf[1] = jpeg_alloc_decoder_mem(BUF_SIZE, &mem_cfg, &actual);
    if (!g_outbuf[0]) g_outbuf[0] = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_outbuf[1]) g_outbuf[1] = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_outbuf[0] || !g_outbuf[1]) { ESP_LOGE(TAG, "Dual buffer alloc fail"); return -1; }

    ESP_LOGI(TAG, "Display pipeline ready");
    return 0;
}

void display_pipeline_deinit(void)
{
    if (g_jpeg) {
        jpeg_del_decoder_engine(g_jpeg);
        g_jpeg = NULL;
    }
    free(g_outbuf[0]); g_outbuf[0] = NULL;
    free(g_outbuf[1]); g_outbuf[1] = NULL;
    display_backend_deinit();
    ESP_LOGI(TAG, "Display pipeline deinitialized");
}

int display_pipeline_feed_jpeg(const uint8_t *data, size_t len)
{
    if (!g_jpeg || !g_outbuf[0]) return -1;

    /* 获取图片信息 */
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(data, len, &info) != ESP_OK) return -1;
    if (info.width == 0 || info.height == 0 || info.width > LCD_W || info.height > LCD_H)
        return -1;

    /* 解码到当前缓冲 */
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    if (jpeg_decoder_process(g_jpeg, &dec_cfg, (uint8_t *)data, len,
                             g_outbuf[g_buf_idx], BUF_SIZE, &out_len) != ESP_OK)
        return -1;

    /* 刷屏 */
    display_backend_flush((uint16_t *)g_outbuf[g_buf_idx]);
    g_buf_idx ^= 1;
    return 0;
}

esp_lcd_panel_handle_t display_pipeline_get_panel(void)
{
    return display_backend_get_panel();
}

uint16_t *display_pipeline_get_fb(void)
{
    return (uint16_t *)g_outbuf[0];
}

void display_pipeline_draw_boot_screen(const uint8_t *jpeg_data, size_t len)
{
    if (!g_jpeg || !g_outbuf[0]) return;

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
        display_backend_flush((uint16_t *)g_outbuf[0]);
        ESP_LOGI(TAG, "Boot screen displayed");
    }
}
