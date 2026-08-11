/** 舵机驱动实现 — LEDC PWM 50Hz */
#include "servo.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "servo";

#define SERVO_FREQ_HZ   50
#define SERVO_MIN_US    500     /* -90° */
#define SERVO_MAX_US    2500    /* +90° */
#define SERVO_CENTER_US 1500

static uint32_t us_to_duty(uint32_t us);

typedef struct {
    gpio_num_t pin;
    ledc_timer_t timer;
    ledc_channel_t channel;
} servo_map_t;

/* GPIO24/25; GPIO28/29 (SERVO3/4) 预留接口可在此追加 */
static const servo_map_t SERVO_MAP[] = {
    { PIN_SERVO_YAW,   LEDC_TIMER_1, LEDC_CHANNEL_1 },
    { PIN_SERVO_PITCH, LEDC_TIMER_1, LEDC_CHANNEL_2 },
};
#define SERVO_COUNT (sizeof(SERVO_MAP) / sizeof(SERVO_MAP[0]))

int servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer config fail");

    for (int i = 0; i < (int)SERVO_COUNT; i++) {
        ledc_channel_config_t ch = {
            .gpio_num   = SERVO_MAP[i].pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = SERVO_MAP[i].channel,
            .timer_sel  = SERVO_MAP[i].timer,
            .duty       = us_to_duty(SERVO_CENTER_US),   /* 回中再使能 */
            .hpoint     = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "channel %d config fail", i);
        ESP_LOGI(TAG, "Servo%d GPIO%d -> center", i, SERVO_MAP[i].pin);
    }
    return 0;
}

static uint32_t us_to_duty(uint32_t us)
{
    /* 14-bit duty: 满量程 16383 = 20ms */
    return (us * 16383) / 20000;
}

void servo_set_angle(int index, int angle_deg)
{
    if (index < 0 || index >= (int)SERVO_COUNT) return;
    if (angle_deg < -90) angle_deg = -90;
    if (angle_deg > 90)  angle_deg = 90;

    uint32_t us = SERVO_CENTER_US + (uint32_t)((angle_deg * 1000) / 90);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_MAP[index].channel, us_to_duty(us));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_MAP[index].channel);
}

int servo_self_test(void)
{
    ESP_LOGI(TAG, "Self-test: 扫动 YAW/PITCH -90°→+90°→回中 ×2…");
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < (int)SERVO_COUNT; i++) {
            for (int a = -90; a <= 90; a += 15) {
                servo_set_angle(i, a);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            servo_set_angle(i, 0);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return 0;
}
