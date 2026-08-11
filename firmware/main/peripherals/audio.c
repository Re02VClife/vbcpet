/** 音频驱动实现 — I2S0 全双工 (MAX98357A + ZTS6672×2) */
#include "audio.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio";

/* AUDIO_SAMPLE_RATE 来自 board_config.h */
#define AUDIO_BITS         16

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;

int audio_init(void)
{
    /* 1. I2S 通道 (TX+RX 全双工) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "new channel fail");

    /* 2. std mode 配置 */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_BITS, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk       = I2S_GPIO_UNUSED,
            .bclk       = PIN_I2S_BCLK,
            .ws         = PIN_I2S_WS,
            .dout       = PIN_I2S_DOUT,   /* → MAX98357A DIN */
            .din        = PIN_I2S_DIN,    /* ← ZTS6672×2 共用 */
            .invert_flags = {
                .mclk_inv = false, .bclk_inv = false, .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "tx init fail");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "rx init fail");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable fail");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx enable fail");

    /* 3. PA_EN 功放控制 (默认关闭) */
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << PIN_PA_EN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* 板上已有 100k 下拉 */
    };
    gpio_config(&pa);
    audio_pa_enable(false);

    ESP_LOGI(TAG, "Audio ready: %dHz %d-bit stereo (BCLK=%d WS=%d DOUT=%d DIN=%d PA_EN=%d)",
             AUDIO_SAMPLE_RATE, AUDIO_BITS, PIN_I2S_BCLK, PIN_I2S_WS,
             PIN_I2S_DOUT, PIN_I2S_DIN, PIN_PA_EN);
    return 0;
}

void audio_pa_enable(bool on)
{
    gpio_set_level(PIN_PA_EN, on ? 1 : 0);
    ESP_LOGI(TAG, "PA %s", on ? "ON" : "off");
}

int audio_play(const int16_t *pcm, size_t samples)
{
    if (!s_tx || !pcm) return -1;
    size_t written = 0;
    esp_err_t ret = i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t), &written, 100);
    return ret == ESP_OK ? 0 : -1;
}

int audio_self_test(void)
{
    /* 1. 440Hz 蜂鸣 ×3 (立体声, 3s) */
    ESP_LOGI(TAG, "Self-test: 440Hz 蜂鸣 ×3…");
    audio_pa_enable(true);
    const int SR = AUDIO_SAMPLE_RATE;
    int16_t tone[SR / 4];   /* 250ms */
    for (int i = 0; i < SR / 4; i++) {
        int16_t v = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / SR) * 8000.0f);
        tone[i] = v;
    }
    for (int n = 0; n < 3; n++) {
        int16_t stereo[SR / 4 * 2];
        for (int i = 0; i < SR / 4; i++) {
            stereo[i*2]   = tone[i];   /* L */
            stereo[i*2+1] = tone[i];   /* R (MAX98357A SD_MODE=HIGH 播 R) */
        }
        i2s_channel_write(s_tx, stereo, sizeof(stereo), NULL, 100);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* 2. 麦克风回放 2s: 读 RX → 写 TX */
    ESP_LOGI(TAG, "Self-test: 麦克风回放 2s (对着麦说话, 喇叭应能听到)…");
    int16_t buf[512 * 2];
    int64_t deadline = xTaskGetTickCount() * portTICK_PERIOD_MS + 2000;
    while (xTaskGetTickCount() * portTICK_PERIOD_MS < deadline) {
        size_t read = 0, written = 0;
        if (i2s_channel_read(s_rx, buf, sizeof(buf), &read, 100) == ESP_OK && read > 0) {
            i2s_channel_write(s_tx, buf, read, &written, 100);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    audio_pa_enable(false);
    return 0;
}
