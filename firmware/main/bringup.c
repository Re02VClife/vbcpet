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

/* ── 状态灯 (SK6812 ×4, 同色, 低亮度 7%) ── */
#define LED_DIM (uint8_t)(255 * 0.07)   /* 低亮度: 分得清颜色又不刺眼 */
static void led_state(uint8_t r, uint8_t g, uint8_t b)
{
    sk6812_set_all((uint8_t)(r * LED_DIM / 255),
                   (uint8_t)(g * LED_DIM / 255),
                   (uint8_t)(b * LED_DIM / 255));
    sk6812_show();
}
/* 常用状态色: 空OK / 黄warn / 红err / 蓝boot / 青info / 紫nfc / 绿touch */
static void led_all_ok(void)      { led_state(0x00, 0xFF, 0x00); }   /* 绿: 全部正常 */
static void led_warn(void)        { led_state(0xFF, 0xD0, 0x00); }   /* 黄: 部分失败 */
static void led_err(void)         { led_state(0xFF, 0x00, 0x00); }   /* 红: 关键失败 */
static void led_boot(void)        { led_state(0x00, 0x80, 0xFF); }   /* 蓝: 启动中 */
int bringup_init(transport_t *transport)
{
    ESP_LOGI(TAG, "Peripheral bring-up starting…");

    /* 1. 共享 I2C 总线 */
    if (i2c_bus_init() != ESP_OK) ESP_LOGW(TAG, "I2C bus init failed (继续)");

    /* 2. 外设逐个 (容错, 单个失败不影响其他) */
    if (buttons_init(transport) != 0)   ESP_LOGW(TAG, "buttons init failed");
    if (sk6812_init() != 0)             ESP_LOGW(TAG, "SK6812 init failed");
    else led_boot();                    /* 状态灯: 启动中 = 蓝 */
    if (audio_init() != 0)              ESP_LOGW(TAG, "audio init failed");
    if (mpu6500_init() != 0)            ESP_LOGW(TAG, "MPU-6500 init failed (可能未焊/异常)");
    if (nfc_init() != 0)                ESP_LOGW(TAG, "NFC init failed");
    if (sd_card_init() != 0)            ESP_LOGW(TAG, "SD init failed (卡未插或硬件问题)");
    if (servo_init() != 0)              ESP_LOGW(TAG, "servo init failed");
    touch_init();                       /* 触摸地址待确认, 失败不告警 */
    if (usb_host_periph_init() != 0)    ESP_LOGW(TAG, "USB host init failed");

    /* 状态灯自动汇总: 启动蓝 → 全 PASS 绿 / 有 FAIL 黄 (简易: 当前按 USB host 键翻) */
    ESP_LOGI(TAG, "Bring-up done. UART0 控制台: help / test <name> / test all / led <state> / brightness <0-100>");
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
                   "  led <state>         状态灯: ok绿/warn黄/err红/boot蓝/off  (低亮度)\n"
                   "  gp <pin>{h|l|r|z}   GPIO 诊断: h驱动高 l驱动低 r读回 z释放 (如 gp 48h)\n"
                   "  lcd                 强制点屏: 初始化 DSI/ST7102/背光 + 刷测试图案\n"
                   "  bl <0-100>          背光隔离测试 (不碰 DSI, 只开背光 PWM)\n"
                   "  brightness <0-100>  设置背光亮度\n"
                   "  reboot              软件复位\n"
                   "  dl                  强制进下载模式(免 strap, 下个启动)\n");
        } else if (strcmp(cmd, "list") == 0) {
            self_test_list();
        } else if (strcmp(cmd, "test") == 0) {
            if (strcmp(arg2, "all") == 0) {
                int rc = self_test_run_all();
                int pass = (rc >> 16) & 0xFFFF, fail = rc & 0xFFFF;
                if (fail > 0) {
                    printf("状态灯: 黄 (有 %d 项失败)\n", fail);
                    led_warn();
                } else {
                    printf("状态灯: 绿 (全部 %d 项通过)\n", pass);
                    led_all_ok();
                }
            } else if (strlen(arg2) > 0) {
                int r = self_test_run(arg2);
                if (r == 0) led_all_ok(); else led_err();
            } else {
                printf("用法: test <name | all>\n");
            }
        } else if (strcmp(cmd, "led") == 0) {
            if (strcmp(arg2, "ok") == 0)     { led_all_ok();   printf("LED: 绿(全部正常)\n"); }
            else if (strcmp(arg2, "warn") == 0) { led_warn();      printf("LED: 黄(警告)\n"); }
            else if (strcmp(arg2, "err") == 0)  { led_err();       printf("LED: 红(关键失败)\n"); }
            else if (strcmp(arg2, "boot") == 0) { led_boot();      printf("LED: 蓝(启动)\n"); }
            else if (strcmp(arg2, "off") == 0)  { sk6812_clear();  printf("LED: 灭\n"); }
            else { printf("用法: led <ok|warn|err|boot|off>\n"); }
        } else if (strcmp(cmd, "gp") == 0) {
            /* 通用 GPIO 诊断 (单 token, 因控制台只取 cmd+arg2):
             *  gp <pin>h  驱动高
             *  gp <pin>l  驱动低
             *  gp <pin>r  输入读回
             *  gp <pin>z  释放(高阻)
             * 例: gp 48h  → 强制 GPIO48 输出高 (绕开 LEDC 定位 pad) */
            unsigned pin = 0; int mode = 2;
            char sfx = 0;
            if (arg2[0] && arg2[0] != '\0') {
                int n = 0;
                if (sscanf(arg2, "%u%c%n", &pin, &sfx, &n) >= 1) {
                    if (sfx == 'h') mode = 1;
                    else if (sfx == 'l') mode = 0;
                    else if (sfx == 'r') mode = 2;
                    else if (sfx == 'z') mode = 3;
                    else mode = 2;   /* 纯数字默认读回 */
                }
            }
            if (pin <= 51) {
                gpio_config_t cfg = {
                    .pin_bit_mask = (1ULL << pin),
                    .mode = (mode <= 1) ? GPIO_MODE_OUTPUT : ((mode == 2) ? GPIO_MODE_INPUT : GPIO_MODE_DISABLE),
                    .pull_up_en = GPIO_PULLUP_DISABLE,
                    .pull_down_en = GPIO_PULLDOWN_DISABLE,
                    .intr_type = GPIO_INTR_DISABLE,
                };
                esp_err_t e = gpio_config(&cfg);
                if (mode <= 1) gpio_set_level((gpio_num_t)pin, (uint32_t)mode);
                int lvl = -1;
                if (mode == 2) lvl = (int)gpio_get_level((gpio_num_t)pin);
                ESP_LOGW(TAG, "gp %u mode=%d: cfg_err=%d level=%d", pin, mode, (int)e, lvl);
            } else {
                printf("用法: gp <pin>{h|l|r|z}  例: gp 48h\n");
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
        } else if (strcmp(cmd, "rd") == 0) {
            /* 诊断: 真实读一个 I2C 地址的寄存器, 打印 esp_err_t + 读到字节
             * 用法: rd <addr_hex> <reg_hex>  例: rd 68 75 / rd 50 0 */
            unsigned a = 0, rg = 0;
            if (sscanf(arg2, "%x %x", &a, &rg) == 2 && a > 0) {
                ESP_LOGI(TAG, "rd: read from addr=0x%02X reg=0x%02X ...", a, rg);
                i2c_master_dev_handle_t dev = NULL;
                esp_err_t e1 = i2c_master_bus_add_device(i2c_bus_get_handle(),
                    &(i2c_device_config_t){ .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                            .device_address = (uint16_t)a, .scl_speed_hz = I2C_FREQ_HZ }, &dev);
                if (e1 != ESP_OK) { ESP_LOGE(TAG, "add_device err=%d", e1); break; }
                uint8_t regb = (uint8_t)rg, val = 0xAA;
                size_t n = 1;
                esp_err_t e2 = i2c_master_transmit_receive(dev, &regb, 1, &val, n, 50);
                ESP_LOGI(TAG, "rd result: trx_err=%d (0=OK 261=NOT_ERR 258=NOT_FOUND 263=TIMEOUT?) val=0x%02X n=%u",
                         e2, val, (unsigned)n);
                i2c_master_bus_rm_device(dev);
            } else {
                printf("用法: rd <addr_hex> <reg_hex>\n");
            }
        } else if (strcmp(cmd, "lcd") == 0) {
            /* 强制点屏: 绕过 SKIP_DISPLAY, 初始化 DSI + ST7102 + 背光, 再刷测试图案。
             * 屏未接/DSI lane 有问题时会返回错误而不卡死 (esp_lcd_* 各自返回非零)。
             * 若板面曾卡在 DSI HOST busy-wait, 由上层饿死/看门狗兜底。 */
            ESP_LOGW(TAG, "lcd: forcing DSI init ...");
            display_backend_request_init();
            int rc = display_backend_init();
            if (rc != 0) {
                ESP_LOGE(TAG, "lcd: DSI init failed rc=%d (查 FPC/DSI lane 顺序/屏供电)", rc);
            } else {
                uint16_t *fb = malloc(LCD_W * LCD_H * 2);
                if (fb) {
                    for (int y = 0; y < LCD_H; y++)
                        for (int x = 0; x < LCD_W; x++)
                            fb[y * LCD_W + x] = (uint16_t)(0xF800 | ((y * 32) / LCD_H) << 5 | ((x * 32) / LCD_W)); /* R 渐变 + G/B 渐变 */
                    display_backend_flush(fb);
                    free(fb);
                    ESP_LOGI(TAG, "lcd: DSI ready + 渐变图案已刷 (应见彩色竖条)");
                }
                display_backend_set_brightness(100);
                ESP_LOGI(TAG, "lcd: 背光 100%% — 屏应点亮");
            }
        } else if (strcmp(cmd, "bl") == 0) {
            /* 背光隔离测试: 只初始化背光 LEDC + 置亮度, 完全不碰 DSI。
             * 用于区分背光链路 vs DSI 链路的问题。
             * 用法: bl <0-100> */
            int p = atoi(arg2);
            if (p < 0 || p > 100) p = 100;
            ESP_LOGW(TAG, "bl: backlight-only test %d%% (no DSI)", p);
            display_backend_enable_backlight(p);
            ESP_LOGW(TAG, "bl: emitted PWM on GPIO48 duty=%d%% — 看屏背光", p);
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
