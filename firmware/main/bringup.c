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
#include "peripherals/wifi_link.h"
#include "peripherals/c6_flash.h"
#include "esp_ldo_regulator.h"
#include "power_rail.h"
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

/* ── 内部 LDO 调压 (诊断用) ──
 * P4 有 4 个可调 LDO (LDO_VO1~VO4), 各自给不同电路供电。本板实测:
 *   LDO_VO3 = 2500mV  DSI PHY      (display_pipeline_dsi.c 已在用)
 *   LDO_VO4 = ~1240mV SDMMC IO 组  (GPIO39~48 = SD1_CDATA0-7/CCLK/CCMD)
 *                                    ⇒ 复位后没人配它, 停在默认低压,
 *                                      导致 SD 卡 / PA_EN(GPIO46) / MPU_INT(GPIO47) / BL_PWM(GPIO48) 全部只有 ~1.2V
 * 由 esp_ldo_acquire_channel() 设定; 通道设了 adjustable 之后可反复调压。
 * 参考: IDF components/sdmmc/include/sd_pwr_ctrl_by_on_chip_ldo.h
 *       "set to 4 is the LDO_VO4 is connected to power the SDMMC IO"
 */
static esp_ldo_channel_handle_t s_ldo_chan[5] = { NULL };   /* index 1..4 */

static void ldo_cmd(int chan, int mv)
{
    if (chan < 1 || chan > 4) {
        printf("用法: ldo <1-4> <mv>    例: ldo 4 3300\n"
               "      ldo <1-4>         查看该通道是否已 acquire\n");
        return;
    }
    /* 通道 4 已由 app_main 的 ldo_vo4_init() acquire (adjustable 通道不能 acquire 两次),
     * 这里复用共享 handle 才能改压, 否则 acquire 会失败。 */
    if (chan == 4 && !s_ldo_chan[4] && g_ldo_vo4) s_ldo_chan[4] = g_ldo_vo4;

    if (mv <= 0) {
        printf("LDO_VO%d: %s\n", chan,
               s_ldo_chan[chan] ? "已 acquire (用 ldo <chan> <mv> 改压)" : "未 acquire");
        return;
    }
    if (chan == 3) printf("注意: LDO_VO3 供 DSI PHY (当前 2500mV), 改动会影响显示\n");
    if (chan == 4) printf("注意: LDO_VO4 供 SDMMC IO 组 (GPIO39~48), 3300 = 标准 SD 电平\n");

    esp_err_t err;
    if (s_ldo_chan[chan]) {
        err = esp_ldo_channel_adjust_voltage(s_ldo_chan[chan], mv);
        printf("LDO_VO%d adjust -> %d mV: %s\n", chan, mv,
               err == ESP_OK ? "OK" : esp_err_to_name(err));
    } else {
        esp_ldo_channel_config_t cfg = {
            .chan_id    = chan,
            .voltage_mv = mv,
            .flags.adjustable = true,
        };
        err = esp_ldo_acquire_channel(&cfg, &s_ldo_chan[chan]);
        printf("LDO_VO%d acquire @ %d mV: %s\n", chan, mv,
               err == ESP_OK ? "OK" : esp_err_to_name(err));
        if (err != ESP_OK) s_ldo_chan[chan] = NULL;
    }
    printf("  现在量 pin66 看 ESP_LDO_VO4 实际输出; pin76 (GPIO48) 应跟着变成 mv 左右\n");
}

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

#if CONFIG_VBCPET_ENABLE_WIFI
    /* WiFi = 模组内 ESP32-C6 (esp-hosted/SDIO), P4 无原生 WiFi */
    wifi_link_boot();   /* 只打印提示, 不主动初始化 (避免 C6 没固件时 boot 期出问题) */
    c6_flash_boot_hook();   /* 可选: 开机自动烧 C6 (绕开僵死的控制台) */
#endif

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

        char cmd[32] = {0}, arg2[32] = {0}, rest[96] = {0};
        sscanf(line, "%31s %31s", cmd, arg2);
        {   /* rest = cmd 之后的整行剩余 (给 "wifi conn <ssid> <pass>" 这类多参数命令) */
            const char *p = line;
            while (*p && !isspace((unsigned char)*p)) p++;
            while (*p && isspace((unsigned char)*p)) p++;
            strncpy(rest, p, sizeof(rest) - 1);
        }

        if (strcmp(cmd, "help") == 0) {
            printf("命令:\n"
                   "  help                本帮助\n"
                   "  list                列出可用自检\n"
                   "  test <name>         运行单个自检 (buttons/leds/touch/audio/imu/sd/servo/nfc/usb)\n"
                   "  test all            运行全部自检\n"
                   "  scan                扫描 I2C 总线 (FOUND/NACK/TIMEOUT)\n"
                   "  led <state>         状态灯: ok绿/warn黄/err红/boot蓝/off  (低亮度)\n"
                   "  gp <pin>{h|l|r|z}   GPIO 诊断: h驱动高 l驱动低 r读回 z释放 (如 gp 48h)\n"
                   "  ldo <1-4> <mv>      设置内部 LDO 通道电压 (如 ldo 4 3300 = SDMMC IO 拉到 3.3V)\n"
                   "  lcd                 强制点屏: 初始化 DSI/ST7102/背光 + 刷测试图案\n"
                   "  bl <0-100>          背光隔离测试 (不碰 DSI, 只开背光 PWM)\n"
                   "  brightness <0-100>  设置背光亮度\n"
                   "  wifi <sub>          WiFi: scan/info/conn [ssid pass]/status (C6/esp-hosted)\n"
                   "  c6flash [rom|baud]  把 main/c6_merged.bin 烧进模组里的 C6 (P4 当编程器)\n"
                   "  c6mon [sec]         被动监听 C6 的 UART (看它有没有在跑)\n"
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
        } else if (strcmp(cmd, "ldo") == 0) {
            /* ldo <chan> <mv>  —— arg2 = 通道号, rest 里第二个 token = 毫伏 */
            int chan = atoi(arg2);
            int mv = 0;
            {
                const char *p = rest;
                while (*p && !isspace((unsigned char)*p)) p++;   /* 跳过通道号 */
                while (*p && isspace((unsigned char)*p)) p++;
                mv = atoi(p);
            }
            ldo_cmd(chan, mv);
        } else if (strcmp(cmd, "scan") == 0) {
            i2c_bus_scan();
        } else if (strcmp(cmd, "wifi") == 0) {
            wifi_link_cmd(arg2, rest);
        } else if (strcmp(cmd, "c6flash") == 0) {
            c6_flash_cmd(arg2, rest);
        } else if (strcmp(cmd, "c6mon") == 0) {
            c6_flash_monitor(*rest ? atoi(rest) : 10);
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
