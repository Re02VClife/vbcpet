/** 显示管线 — JPEG 解码 + 单缓冲 + 后端抽象
 *
 * 单缓冲设计: JPEG 解码到 fb, 文字叠加到同一 fb, 统一 flush。
 * 解码只要 ~1ms, 不需要双缓冲复杂度。
 */
#include "display_pipeline.h"
#include "display_backend.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "disp";

static jpeg_decoder_handle_t g_jpeg = NULL;
static uint8_t *g_fb = NULL;  /* 单帧缓冲 */

/* JPEG 解码输出格式 = 帧缓冲格式 (Kconfig) */
#ifdef CONFIG_VBCPET_DISPLAY_FORMAT_RGB888
#define JPEG_DEC_OUT_FMT  JPEG_DECODE_OUT_FORMAT_RGB888
#else
#define JPEG_DEC_OUT_FMT  JPEG_DECODE_OUT_FORMAT_RGB565
#endif

/* 颜色修正 v2: 面板 RGB 顺序无改善后, 改解码器输出的 R/B 顺序 (JPEG_DEC_RGB_ELEMENT_ORDER_BGR) */
#define JPEG_DEC_RGB_ORDER  JPEG_DEC_RGB_ELEMENT_ORDER_BGR

int display_pipeline_init(void)
{
    ESP_LOGI(TAG, "Initializing display pipeline (%dx%d)...", LCD_W, LCD_H);

    if (display_backend_init() != 0) { ESP_LOGE(TAG, "Backend fail"); return -1; }

    jpeg_decode_engine_cfg_t eng_cfg = { .timeout_ms = 2000 };
    if (jpeg_new_decoder_engine(&eng_cfg, &g_jpeg) != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decoder fail"); return -1;
    }

    /* 帧缓冲: 480×800 RGB565 = 768KB, 优先 PSRAM (内部 SRAM 放不下) */
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    size_t actual = 0;
    g_fb = jpeg_alloc_decoder_mem(BUF_SIZE, &mem_cfg, &actual);
    if (!g_fb) g_fb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_fb) g_fb = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_fb) { ESP_LOGE(TAG, "FB alloc fail (%d B)", (int)BUF_SIZE); return -1; }
    ESP_LOGI(TAG, "FB allocated: %d B", (int)BUF_SIZE);

    ESP_LOGI(TAG, "Display pipeline ready (single buffer)");
    return 0;
}

void display_pipeline_deinit(void)
{
    if (g_jpeg) { jpeg_del_decoder_engine(g_jpeg); g_jpeg = NULL; }
    free(g_fb); g_fb = NULL;
    display_backend_deinit();
}

int display_pipeline_feed_jpeg(const uint8_t *data, size_t len)
{
    if (!g_jpeg || !g_fb) return -1;

    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(data, len, &info) != ESP_OK) return -1;
    if (info.width == 0 || info.height == 0 || info.width > LCD_W || info.height > LCD_H)
        return -1;

    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DEC_OUT_FMT,
        .rgb_order     = JPEG_DEC_RGB_ORDER,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    if (jpeg_decoder_process(g_jpeg, &dec_cfg, (uint8_t *)data, len,
                             g_fb, BUF_SIZE, &out_len) != ESP_OK)
        return -1;

    /* 不刷屏 — 调用者叠加文字后统一 flush */
    return 0;
}

void display_pipeline_flush(void)
{
    if (g_fb) display_backend_flush((uint16_t *)g_fb);
}

esp_lcd_panel_handle_t display_pipeline_get_panel(void)
{
    return display_backend_get_panel();
}

uint16_t *display_pipeline_get_fb(void)
{
    return (uint16_t *)g_fb;
}

void display_pipeline_draw_boot_screen(const uint8_t *jpeg_data, size_t len)
{
    if (!g_jpeg || !g_fb) return;

    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(jpeg_data, len, &info) != ESP_OK) return;
    if (info.width == 0 || info.height == 0 || info.width > LCD_W || info.height > LCD_H)
        return;

    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DEC_OUT_FMT,
        .rgb_order     = JPEG_DEC_RGB_ORDER,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    if (jpeg_decoder_process(g_jpeg, &dec_cfg, (uint8_t *)jpeg_data, len,
                             g_fb, BUF_SIZE, &out_len) == ESP_OK) {
        display_backend_flush((uint16_t *)g_fb);
        ESP_LOGI(TAG, "Boot screen displayed");
    }
}
