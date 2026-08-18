/** Bring-up 框架实现 — 外设逐个初始化 + UART0 命令控制台 */
#include "bringup.h"
#include "board_config.h"
#include "display_backend.h"
#include "peripherals/i2c_bus.h"
#include "peripherals/buttons.h"
#include "peripherals/sk6812.h"
#include "peripherals/touch.h"
#include "peripherals/audio.h"
#include "peripherals/mpu6500.h"
#include "peripherals/sd_card.h"
#include "peripherals/servo.h"
#include "peripherals/nfc.h"
#include "peripherals/usb_host.h"
#include "peripherals/self_test.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/lp_system_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "bringup";

/* 上电时序注释 (对照 README 打样疑点):
 *   3V3/Buck → 模组 → 显示(屏/背光, 见 display_pipeline_init) → I2C 总线 → 外设逐个
 * ⚠️ 板到后优先核对: D1 pin5=VBUS_IN(电池模式 USB-A 瘫痪) + USB_DP/DM 线宽
 */
int bringup_init(transport_t *transport)
{
    ESP_LOGI(TAG, "Peripheral bring-up starting…");

    /* 1. 共享 I2C 总线 */
    if (i2c_bus_init() != ESP_OK) ESP_LOGW(TAG, "I2C bus init failed (继续)");

    /* 2. 外设逐个 (容错, 单个失败不影响其他) */
    if (buttons_init(transport) != 0)   ESP_LOGW(TAG, "buttons init failed");
    if (sk6812_init() != 0)             ESP_LOGW(TAG, "SK6812 init failed");
    if (audio_init() != 0)              ESP_LOGW(TAG, "audio init failed");
    if (mpu6500_init() != 0)            ESP_LOGW(TAG, "MPU-6500 init failed (可能未焊/异常)");
    if (nfc_init() != 0)                ESP_LOGW(TAG, "NFC init failed");
    if (sd_card_init() != 0)            ESP_LOGW(TAG, "SD init failed (卡未插或硬件问题)");
    if (servo_init() != 0)              ESP_LOGW(TAG, "servo init failed");
    touch_init();                       /* 触摸地址待确认, 失败不告警 */
    if (usb_host_periph_init() != 0)    ESP_LOGW(TAG, "USB host init failed");

    ESP_LOGI(TAG, "Bring-up done. UART0 控制台: help / test <name> / test all / brightness <0-100>");
    return 0;
}

/* ── UART0 控制台任务 (stdin = UART0) ── */
static void console_task(void *arg)
{
    (void)arg;
    char line[128];
    int n = 0;   /* 逐字符拼行: fgets 会因 UART 短读返回半行 (test all 被拆), 需自缓冲到换行 */
    while (1) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n == 0) continue;            /* 空行 / 多换行 */
            line[n] = '\0';
            n = 0;
        } else {
            if (n < (int)sizeof(line) - 1) line[n++] = (char)c;
            continue;                        /* 未到行尾, 继续收 */
        }

        char cmd[32] = {0}, arg2[32] = {0};
        sscanf(line, "%31s %31s", cmd, arg2);

        if (strcmp(cmd, "help") == 0) {
            printf("命令:\n"
                   "  help                本帮助\n"
                   "  list                列出可用自检\n"
                   "  test <name>         运行单个自检 (buttons/leds/touch/audio/imu/sd/servo/nfc/usb)\n"
                   "  test all            运行全部自检\n"
                   "  scan                扫描 I2C 总线 (FOUND/NACK/TIMEOUT)\n"
                   "  brightness <0-100>  设置背光亮度\n"
                   "  reboot              软件复位\n"
                   "  dl                  强制进下载模式(免 strap, 下个启动)\n");
        } else if (strcmp(cmd, "list") == 0) {
            self_test_list();
        } else if (strcmp(cmd, "test") == 0) {
            if (strcmp(arg2, "all") == 0) {
                self_test_run_all();
            } else if (strlen(arg2) > 0) {
                self_test_run(arg2);
            } else {
                printf("用法: test <name | all>\n");
            }
        } else if (strcmp(cmd, "scan") == 0) {
            i2c_bus_scan();
        } else if (strcmp(cmd, "reboot") == 0) {
            ESP_LOGI(TAG, "software reboot ...");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else if (strcmp(cmd, "dl") == 0) {
            /* 软件强制下载模式: 置 LP_SYSTEM_REG_SYS_CTRL bit2(FORCE_DOWNLOAD_BOOT)
             * 后复位, 下一次启动直接进 ROM 下载模式, 完全绕过 GPIO35/36 硬 strap。
             * (EFUSE_DIS_FORCE_DOWNLOAD 默认 0=允许; 位注释 need_des, 待实板验证) */
            ESP_LOGW(TAG, "force-download: set FORCE_DOWNLOAD_BOOT bit + reset ...");
            REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        } else if (strcmp(cmd, "brightness") == 0) {
            int p = atoi(arg2);
            if (p >= 0 && p <= 100) {
                display_backend_set_brightness(p);
                printf("背光 %d%%\n", p);
            } else {
                printf("用法: brightness <0-100>\n");
            }
        } else {
            printf("未知命令: %s (输入 help)\n", cmd);
        }
    }
}

/** 启动控制台任务 (由 main 在 bringup_init 后调用) */
void bringup_start_console(void)
{
    xTaskCreate(console_task, "br-up-cmd", 8192, NULL, 4, NULL);   /* 8KB: 自检调用链 (audio 堆分配后仍留余量) */
}
