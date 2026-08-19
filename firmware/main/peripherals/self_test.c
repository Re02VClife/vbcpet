/** Bring-up 自检注册表实现 */
#include "self_test.h"
#include "buttons.h"
#include "sk6812.h"
#include "touch.h"
#include "audio.h"
#include "mpu6500.h"
#include "sd_card.h"
#include "servo.h"
#include "nfc.h"
#include "usb_host.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "selftest";

typedef struct {
    const char *name;
    int (*run)(void);
} self_test_entry_t;

static const self_test_entry_t ENTRIES[] = {
    { "buttons",  buttons_self_test },
    { "leds",     sk6812_self_test },
    { "touch",    touch_self_test },
    { "audio",    audio_self_test },
    { "imu",      mpu6500_self_test },
    { "sd",       sd_card_self_test },
    { "servo",    servo_self_test },
    { "nfc",      nfc_self_test },
    { "usb",      usb_host_periph_self_test },
    { "display",  NULL },   /* display 自检 = 后端初始化本身, 由 main 负责 */
};
#define ENTRY_COUNT (sizeof(ENTRIES) / sizeof(ENTRIES[0]))

void self_test_list(void)
{
    ESP_LOGI(TAG, "可用自检:");
    for (int i = 0; i < (int)ENTRY_COUNT; i++) {
        ESP_LOGI(TAG, "  test %-8s %s", ENTRIES[i].name,
                 ENTRIES[i].run ? "" : "(无独立自检, 随系统初始化)");
    }
}

int self_test_run(const char *name)
{
    for (int i = 0; i < (int)ENTRY_COUNT; i++) {
        if (strcasecmp(name, ENTRIES[i].name) == 0) {
            if (!ENTRIES[i].run) { ESP_LOGW(TAG, "%s 无独立自检", name); return 0; }
            ESP_LOGI(TAG, "==== 自检 [%s] 开始 ====", ENTRIES[i].name);
            int r = ENTRIES[i].run();
            ESP_LOGI(TAG, "==== 自检 [%s] %s ====", ENTRIES[i].name, r == 0 ? "PASS" : "FAIL");
            return r;
        }
    }
    ESP_LOGE(TAG, "未知自检: '%s' (输入 help 看列表)", name);
    return -1;
}

int self_test_run_all(void)
{
    int pass = 0, fail = 0;
    for (int i = 0; i < (int)ENTRY_COUNT; i++) {
        if (!ENTRIES[i].run) continue;
        ESP_LOGI(TAG, "==== 自检 [%s] 开始 ====", ENTRIES[i].name);
        int r = ENTRIES[i].run();
        ESP_LOGI(TAG, "==== 自检 [%s] %s ====", ENTRIES[i].name, r == 0 ? "PASS" : "FAIL");
        if (r == 0) pass++; else fail++;
    }
    ESP_LOGI(TAG, "自检汇总: %d 通过, %d 失败", pass, fail);
    return (pass << 16) | fail;
}
