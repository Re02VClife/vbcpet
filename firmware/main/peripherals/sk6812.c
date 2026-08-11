/** SK6812 驱动实现 — RMT 直发 WS2812B 帧 (GRB, 800kHz) */
#include "sk6812.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "leds";

/* WS2812B 时序 @ RMT 80MHz (1 tick = 12.5ns) */
#define T0H  28   /* 0.35us */
#define T0L  64   /* 0.80us */
#define T1H  56   /* 0.70us */
#define T1L  48   /* 0.60us */

static rmt_channel_handle_t s_tx  = NULL;
static rmt_encoder_handle_t s_enc = NULL;
static uint8_t s_pixels[SK6812_COUNT][3];   /* GRB */

int sk6812_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num          = PIN_SK6812_DIN,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 80 * 1000 * 1000,
        .mem_block_symbols = SK6812_COUNT * 24 + 8,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&cfg, &s_tx), TAG, "rmt tx channel fail");
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(NULL, &s_enc), TAG, "copy encoder fail");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx), TAG, "rmt enable fail");

    memset(s_pixels, 0, sizeof(s_pixels));
    ESP_LOGI(TAG, "SK6812 x%d ready (GPIO%d)", SK6812_COUNT, PIN_SK6812_DIN);
    return 0;
}

void sk6812_set_color(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < 0 || index >= SK6812_COUNT) return;
    s_pixels[index][0] = g;   /* GRB */
    s_pixels[index][1] = r;
    s_pixels[index][2] = b;
}

void sk6812_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < SK6812_COUNT; i++) sk6812_set_color(i, r, g, b);
}

void sk6812_clear(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
    sk6812_show();
}

void sk6812_show(void)
{
    if (!s_tx || !s_enc) return;

    rmt_symbol_word_t sym[SK6812_COUNT * 24];
    for (int i = 0; i < SK6812_COUNT; i++) {
        uint32_t grb = ((uint32_t)s_pixels[i][0] << 16) |
                       ((uint32_t)s_pixels[i][1] << 8)  |
                       ((uint32_t)s_pixels[i][2]);
        for (int b = 0; b < 24; b++) {
            bool one = (grb >> (23 - b)) & 1;
            sym[i * 24 + b].level0    = 1;
            sym[i * 24 + b].duration0 = one ? T1H : T0H;
            sym[i * 24 + b].level1    = 0;
            sym[i * 24 + b].duration1 = one ? T1L : T0L;
        }
    }
    rmt_transmit_config_t tx_cfg = { .loop_count = 0, .flags.eot_level = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_tx, s_enc, sym, sizeof(sym), &tx_cfg));
    rmt_tx_wait_all_done(s_tx, 100);
}

int sk6812_self_test(void)
{
    ESP_LOGI(TAG, "Self-test: 彩虹循环 8 次…");
    for (int round = 0; round < 8; round++) {
        for (int i = 0; i < SK6812_COUNT; i++) {
            uint8_t r = 0, g = 0, b = 0;
            switch ((round + i) % 3) {
            case 0: r = 255; break;
            case 1: g = 255; break;
            case 2: b = 255; break;
            }
            sk6812_set_color(i, r, g, b);
        }
        sk6812_show();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    sk6812_clear();
    return 0;
}
