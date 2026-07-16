/** audio_codec — ES7210 + ES8311 via esp_codec_dev (小智方案) */
#include "audio_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

/* ── I2C 引脚 (小智标准参考; P4 自绘板需重新映射) ── */
#define I2C_NUM   I2C_NUM_0
#define I2C_SDA   GPIO_NUM_8
#define I2C_SCL   GPIO_NUM_18
#define I2C_FREQ  100000

/* I2S 引脚 */
#define I2S_NUM         I2C_NUM_0
#define I2S_MCLK        GPIO_NUM_5
#define I2S_BCLK        GPIO_NUM_16
#define I2S_WS          GPIO_NUM_7
#define I2S_DOUT        GPIO_NUM_15   /* → ES8311 */
#define I2S_DIN         GPIO_NUM_6    /* ← ES7210 */
#define PA_PIN          GPIO_NUM_46

/* 采样率 */
#define IN_SAMPLE_RATE   16000
#define OUT_SAMPLE_RATE  24000

/* I2C 地址 */
#define ES8311_ADDR  (0x18)
#define ES7210_ADDR  (0x40)

static esp_codec_dev_handle_t s_out_codec = NULL;  /* ES8311 */
static esp_codec_dev_handle_t s_in_codec  = NULL;  /* ES7210 */

int audio_codec_init(void) {
    /* 1. I2C 总线 */
    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA,
        .scl_io_num       = I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized (SDA=%d SCL=%d)", I2C_SDA, I2C_SCL);

    /* 2. ES8311 DAC (输出) */
    esp_codec_dev_cfg_t out_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if  = {
            .cfg_type    = CODEC_IF_CFG_TYPE_I2C,
            .i2c         = { .port = I2C_NUM, .addr = ES8311_ADDR },
        },
    };
    s_out_codec = esp_codec_dev_new(&out_cfg);
    assert(s_out_codec);
    esp_codec_set_out_vol(s_out_codec, 60.0);   /* 60% 音量 */
    ESP_ERROR_CHECK(esp_codec_dev_open(s_out_codec, &(esp_codec_dev_sample_info_t){
        .sample_rate = OUT_SAMPLE_RATE,
        .channel     = 1,
        .bits_per_sample = 16,
    }));
    ESP_LOGI(TAG, "ES8311 DAC ready @%dHz", OUT_SAMPLE_RATE);

    /* 3. ES7210 ADC (输入, 4ch TDM; CH1=AEC参考) */
    esp_codec_dev_cfg_t in_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if  = {
            .cfg_type    = CODEC_IF_CFG_TYPE_I2C,
            .i2c         = { .port = I2C_NUM, .addr = ES7210_ADDR },
        },
    };
    s_in_codec = esp_codec_dev_new(&in_cfg);
    assert(s_in_codec);
    ESP_ERROR_CHECK(esp_codec_dev_open(s_in_codec, &(esp_codec_dev_sample_info_t){
        .sample_rate = IN_SAMPLE_RATE,
        .channel     = 4,
        .bits_per_sample = 16,
    }));
    ESP_LOGI(TAG, "ES7210 ADC ready @%dHz, 4ch TDM", IN_SAMPLE_RATE);

    /* 4. 功放初始化 (低有效省电) */
    gpio_config_t pa_cfg = { .pin_bit_mask = BIT64(PA_PIN), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&pa_cfg);
    audio_pa_enable(false);
    ESP_LOGI(TAG, "PA GPIO%d ready", PA_PIN);

    return 0;
}

void audio_codec_deinit(void) {
    if (s_out_codec) { esp_codec_dev_close(s_out_codec); esp_codec_dev_delete(s_out_codec); }
    if (s_in_codec)  { esp_codec_dev_close(s_in_codec);  esp_codec_dev_delete(s_in_codec);  }
    i2c_driver_delete(I2C_NUM);
}

bool audio_start_recording(void) {
    return s_in_codec && esp_codec_dev_open(s_in_codec, NULL) == ESP_OK;
}
bool audio_stop_recording(void) {
    return s_in_codec && esp_codec_dev_close(s_in_codec) == ESP_OK;
}
bool audio_start_playback(void) {
    return s_out_codec && esp_codec_dev_open(s_out_codec, NULL) == ESP_OK;
}
bool audio_stop_playback(void) {
    return s_out_codec && esp_codec_dev_close(s_out_codec) == ESP_OK;
}
void audio_pa_enable(bool on) { gpio_set_level(PA_PIN, on ? 1 : 0); }
