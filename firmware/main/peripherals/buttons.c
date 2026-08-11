/** 按键驱动实现 — GPIO 轮询 + 消抖 + 上报 PC */
#include "buttons.h"
#include "board_config.h"
#include "protocol.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "btns";

typedef struct {
    gpio_num_t pin;
    const char *key;        /* PC 端按键名 (common/protocol.py KEYS) */
} btn_map_t;

static const btn_map_t BTN_MAP[] = {
    { PIN_BTN_YES,         "yes" },
    { PIN_BTN_NO,          "no" },
    { PIN_BTN_YES_ALWAYS,  "yes_always" },
    { PIN_BTN_PANEL,       "panel" },
};
#define BTN_COUNT (sizeof(BTN_MAP) / sizeof(BTN_MAP[0]))

static transport_t *g_transport = NULL;

/* ── 上报 button 事件 (CTRL: 前缀) ── */
static void send_button(const char *key)
{
    if (!g_transport) return;
    char *json = vbc_build_button(key, NULL);
    if (!json) return;

    char prefix[] = "CTRL:";
    int  plen = (int)strlen(prefix);
    int  jlen = (int)strlen(json);
    char *buf = (char *)malloc(plen + jlen + 2);
    if (buf) {
        memcpy(buf, prefix, plen);
        memcpy(buf + plen, json, jlen);
        buf[plen + jlen] = '\n';
        buf[plen + jlen + 1] = '\0';
        g_transport->send(g_transport, (uint8_t *)buf, plen + jlen + 1, 50);
        free(buf);
    }
    free(json);
}

static void buttons_task(void *arg)
{
    (void)arg;
    uint8_t stable[BTN_COUNT] = {0};   /* 0=释放 1=按下 (消抖后) */
    uint8_t raw[BTN_COUNT] = {0};
    uint32_t last_change[BTN_COUNT] = {0};
    const uint32_t DEBOUNCE_MS = 30;

    while (1) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        for (int i = 0; i < (int)BTN_COUNT; i++) {
            uint8_t level = (gpio_get_level(BTN_MAP[i].pin) == 0) ? 1 : 0;  /* 按下=LOW */
            if (level != raw[i]) {
                raw[i] = level;
                last_change[i] = now;
            } else if (level != stable[i] && (now - last_change[i]) >= DEBOUNCE_MS) {
                stable[i] = level;
                if (level) {
                    ESP_LOGI(TAG, "Key '%s' pressed", BTN_MAP[i].key);
                    send_button(BTN_MAP[i].key);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int buttons_init(transport_t *transport)
{
    g_transport = transport;

    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << BTN_MAP[i].pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   /* 板上 10k 上拉, 再加内部上拉保险 */
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&cfg);
        ESP_LOGI(TAG, "Key '%s' GPIO%d ready", BTN_MAP[i].key, BTN_MAP[i].pin);
    }

    BaseType_t ok = xTaskCreate(buttons_task, "btns", 2048, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create fail");
    return 0;
}

int buttons_self_test(void)
{
    ESP_LOGI(TAG, "Self-test: 请逐个按 YES/NO/ALWAYS/PANEL (5s, 按下的键会打印)");
    uint8_t prev[BTN_COUNT] = {0};
    for (int s = 0; s < 50; s++) {
        for (int i = 0; i < (int)BTN_COUNT; i++) {
            uint8_t level = (gpio_get_level(BTN_MAP[i].pin) == 0) ? 1 : 0;
            if (level != prev[i]) {
                prev[i] = level;
                ESP_LOGI(TAG, "  [%s] %s", BTN_MAP[i].key, level ? "PRESS" : "release");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
