/** P4→C6 串口编程器实现 (esp-serial-flasher) */
#include "c6_flash.h"
#include "sdkconfig.h"

#if CONFIG_VBCPET_C6_FLASH
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_loader.h"
#include "esp32_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "c6flash";

/* 合并镜像 (bootloader@0 + 分区表@0x8000 + ota_data@0xd000 + app@0x10000) */
extern const uint8_t c6_merged_bin_start[] asm("_binary_c6_merged_bin_start");
extern const uint8_t c6_merged_bin_end[]   asm("_binary_c6_merged_bin_end");

#define C6_UART_NUM   UART_NUM_1
#define C6_TX         CONFIG_VBCPET_C6_UART_TX_GPIO
#define C6_RX         CONFIG_VBCPET_C6_UART_RX_GPIO
#define C6_BOOT_GPIO  CONFIG_VBCPET_C6_BOOT_GPIO     /* -1 = 直接接 GND */
#define C6_SPARE_A    CONFIG_VBCPET_C6_SPARE_GPIO_A  /* 给库当"复位脚"用, 板子上没接 */
#define C6_SPARE_B    CONFIG_VBCPET_C6_SPARE_GPIO_B  /* 给库当"BOOT 脚"用, 板子上没接 */

static void boot_strap(int level)
{
#if C6_BOOT_GPIO >= 0
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << C6_BOOT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&c);
    gpio_set_level(C6_BOOT_GPIO, level);
    ESP_LOGW(TAG, "C6_IO9(GPIO%d) → %d", C6_BOOT_GPIO, level);
#else
    (void)level;
#endif
}

/* ── C6 唤醒扫描 ────────────────────────────────────────────────
 * 模组没引出 C6 的 EN, 但模组内部很可能把它接到了某个 P4 GPIO (esp_hosted 给 P4 的
 * 默认 CP 复位脚是 GPIO54)。这里把候选脚逐个驱动到 高/低/高阻, 每种组合先**监听 C6 的 TX**
 * (它若被释放复位, 会打印 ROM 启动日志), 再尝试同步。哪个组合有回应, 就是它。
 */
static int s_wake_pin = -1, s_wake_level = -1;

static void dump_bytes(const uint8_t *b, int n, int64_t t_ms);

static void pin_drive(int pin, int level)   /* level: 0/1=输出, -1=高阻 */
{
    if (pin < 0) return;
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << pin,
        .mode = (level < 0) ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&c);
    if (level >= 0) gpio_set_level(pin, level);
}

static const char *lvl_str(int l) { return l > 0 ? "高" : (l < 0 ? "高阻" : "低"); }

/* ── C6 EN/唤醒脚扫描 (2026-09-30 重写) ────────────────────────────────
 * 旧版每轮都 esp_loader_init_serial/deinit ⇒ 反复重装 UART driver。而
 * "GPIO31 刚被 UART 接管"这个瞬态本身就会读出一批垃圾(恒低线上 UART 会
 * 持续报帧错误、填 0x00) —— 旧扫描里那些"127/128 字节满缓冲"很可能有
 * 一半是这个假象, 而不是 C6 在说话。
 *
 * 新版: UART driver 只装一次; 循环里只切候选脚电平, 每次先 flush 再监听,
 * 并把内容按 hex+ASCII 打出来。判据因此变干净: 某个脚/电平之后**稳定出现
 * 可读的 ROM 文本**才说明它真的唤醒了 C6。
 */
static void c6_wake_scan(void)
{
    /* 54 = esp_hosted 对 P4+C6 模组的默认 CP 复位脚; 53/52/0 是旧扫描的候选;
     * 49/50 是 esp_loader 借用的 reset/boot 脚 (已被 c6flash 实测排除, 留作对照) */
    static const int pins[] = { 54, 53, 52, 0, 49, 50 };
    static const int levels[] = { 1, 0, -1 };
    const int n_pins = (int)(sizeof(pins) / sizeof(pins[0]));

    if (uart_driver_delete(C6_UART_NUM) == ESP_OK) ESP_LOGW(TAG, "清掉残留的 UART%d driver", C6_UART_NUM);
    uart_config_t uc = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(C6_UART_NUM, 8192, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install 失败, 扫描中止");
        return;
    }
    uart_param_config(C6_UART_NUM, &uc);
    uart_set_pin(C6_UART_NUM, C6_TX, C6_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_LOGW(TAG, "=== C6 唤醒脚扫描: %d 脚 × {高,低,高阻}, 每态监听 1.5s ===", n_pins);
    int hits = 0;
    for (int i = 0; i < n_pins; i++) {
        for (int j = 0; j < 3; j++) {
            pin_drive(pins[i], levels[j]);
            vTaskDelay(pdMS_TO_TICKS(200));          /* 让电平稳定/让 C6 有时间反应 */
            uart_flush_input(C6_UART_NUM);           /* 只保留这一刻之后的数据 */
            uint8_t raw[256];
            int n = uart_read_bytes(C6_UART_NUM, raw, sizeof raw, pdMS_TO_TICKS(1500));
            ESP_LOGW(TAG, "[扫描] GPIO%d=%s → %d 字节", pins[i], lvl_str(levels[j]), n);
            if (n > 0) {
                dump_bytes(raw, n, 0);
                hits++;
                if (s_wake_pin < 0) { s_wake_pin = pins[i]; s_wake_level = levels[j]; }
            }
        }
    }
    uart_driver_delete(C6_UART_NUM);
    if (hits) {
        ESP_LOGE(TAG, "★ 扫描结束: %d/%d 组合有字节输出 —— 以后续 hex 里是否出现可读 ROM 文本"
                      "(\"ESP-ROM:esp32c6\"/\"waiting for download\"/\"invalid header\")为准", hits, n_pins * 3);
    } else {
        ESP_LOGE(TAG, "★ 扫描结束: 所有组合都是 0 字节 ⇒ 这 6 个脚都不是 C6 的 EN, "
                      "或 C6 本身没有在跑 (查 pin9 接地 / C6 供电)");
    }
}

int c6_flash_run(bool use_stub, uint32_t baud, int wait_seconds)
{
    const uint32_t size = (uint32_t)(c6_merged_bin_end - c6_merged_bin_start);
    ESP_LOGI(TAG, "=== 开始烧 C6: 镜像 %u 字节, UART%d TX=GPIO%d RX=GPIO%d %s ===",
             (unsigned)size, C6_UART_NUM, C6_TX, C6_RX, use_stub ? "(stub)" : "(rom)");
    ESP_LOGW(TAG, "若 C6 里是坏固件/没进下载模式: 现在给板子**断电重上电** (保持 pin9 接地)");

    boot_strap(0);   /* IO9=0 ⇒ C6 复位后进 ROM 下载 (若那根线接到了 P4 上才有作用) */

    esp32_port_t port = {
        .port.ops       = &esp32_uart_ops,
        .baud_rate      = 115200,
        .uart_port      = C6_UART_NUM,
        .uart_rx_pin    = (gpio_num_t)C6_RX,
        .uart_tx_pin    = (gpio_num_t)C6_TX,
        .reset_pin      = (gpio_num_t)C6_SPARE_A,
        .boot_pin       = (gpio_num_t)C6_SPARE_B,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024,
    };
    esp_loader_t loader;
    esp_loader_error_t err = esp_loader_init_serial(&loader, &port.port);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "esp_loader_init_serial 失败: %d", (int)err);
        boot_strap(1);
        return -1;
    }

    esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
    args.sync_timeout = 200;
    args.trials       = 1;                      /* 我们自己循环重试, 便于打印进度 */
    int64_t t0 = esp_timer_get_time();
    int rx_total = 0;                           /* C6 在我们等待期间说了多少字节 */
    while (1) {
        err = use_stub ? esp_loader_connect_with_stub(&loader, &args)
                       : esp_loader_connect(&loader, &args);
        if (err == ESP_LOADER_SUCCESS) break;
        int waited = (int)((esp_timer_get_time() - t0) / 1000000);

        /* 趁 sync 的空档偷读 C6 的 TX: 这能一眼区分"完全静默"和"在 boot loop 刷屏"。
         * 后者说明 C6 根本没进 ROM 下载模式 (IO9 在复位采样时不是 0)。 */
        uint8_t raw[256];
        int got = uart_read_bytes(C6_UART_NUM, raw, sizeof raw, pdMS_TO_TICKS(50));
        if (got > 0) rx_total += got;

        if (waited >= wait_seconds) {
            ESP_LOGE(TAG, "等 %ds 仍没等到 C6 (err=%d), 期间从 C6 TX 收到 %d 字节", waited, (int)err, rx_total);
            if (rx_total > 200) {
                ESP_LOGE(TAG, "★ C6 一直在说话 ⇒ 它有电、在跑 ROM, 但**没进下载模式** "
                              "(ROM 进了下载模式会打完 waiting for download 就停下)。"
                              "首要嫌疑: C6_IO9(模组 pin9) 没有真正接地, 或接触不良 —— "
                              "断电量 pin9↔GND 是否 ~0Ω, 然后整板断电重上电。");
            } else if (rx_total == 0) {
                ESP_LOGE(TAG, "★ C6 一个字都没说 ⇒ 查: pin9 接线 / TX-RX 是否接反 / "
                              "C6 是否有电(pin9,pin10 上电应 ~3.3V)");
            }
            esp_loader_deinit(&loader);
            boot_strap(1);
            return -1;
        }
        if (waited % 5 == 0) ESP_LOGW(TAG, "  等 C6 就绪... %ds (err=%d, C6 已吐 %d 字节)",
                                      waited, (int)err, rx_total);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "已连上 C6: target=%d", (int)esp_loader_get_target(&loader));

    if (use_stub && baud > 115200) {
        if (esp_loader_change_transmission_rate(&loader, baud) == ESP_LOADER_SUCCESS) {
            ESP_LOGI(TAG, "波特率提到 %u", (unsigned)baud);
        } else {
            ESP_LOGW(TAG, "波特率切换失败, 继续用 115200");
        }
    }

    esp_loader_flash_cfg_t cfg = { .offset = 0, .image_size = size, .block_size = 4096 };
    err = esp_loader_flash_start(&loader, &cfg);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "flash_start 失败: %d (C6 flash size/mode 不匹配?)", (int)err);
        esp_loader_deinit(&loader);
        boot_strap(1);
        return -1;
    }

    const uint32_t chunk = 4096;
    uint32_t written = 0;
    int64_t t_start = esp_timer_get_time();
    while (written < size) {
        uint32_t n = (size - written) > chunk ? chunk : (size - written);
        err = esp_loader_flash_write(&loader, &cfg, c6_merged_bin_start + written, n);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "flash_write 失败 @%u: %d", (unsigned)written, (int)err);
            esp_loader_deinit(&loader);
            boot_strap(1);
            return -1;
        }
        written += n;
        if ((written / (128 * 1024)) != ((written - n) / (128 * 1024))) {
            int sec = (int)((esp_timer_get_time() - t_start) / 1000000);
            ESP_LOGI(TAG, "  进度 %u/%u 字节 (%d%%, %ds)", (unsigned)written, (unsigned)size,
                     (int)(written * 100 / size), sec);
        }
        vTaskDelay(1);   /* 让出 CPU, 控制台不卡 */
    }

    err = esp_loader_flash_finish(&loader, &cfg);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "flash_finish 失败: %d", (int)err);
    } else {
        ESP_LOGI(TAG, "✅ C6 烧录完成 (%u 字节, %ds)", (unsigned)size,
                 (int)((esp_timer_get_time() - t_start) / 1000000));
    }
    esp_loader_reset_target(&loader);
    esp_loader_deinit(&loader);
    boot_strap(1);   /* 释放 BOOT strap (若接了 P4) */
    ESP_LOGW(TAG, "下一步: ① 若 pin9 是直接接 GND 的, 请**拔掉那根线** ② 断电重上电 → C6 才会跑 slave 固件");
    return (err == ESP_LOADER_SUCCESS) ? 0 : -1;
}

/** 把一批原始字节打成 "hex + 可读 ASCII" 两行, 便于肉眼认 C6 的 ROM 日志
 *  (2026-09-30 加: 之前 printf("%s") 直接吐二进制, 认不出内容) */
static void dump_bytes(const uint8_t *b, int n, int64_t t_ms)
{
    char hex[3 * 32 + 1], asc[33];
    for (int i = 0; i < n; i += 32) {
        int m = (n - i) > 32 ? 32 : (n - i);
        int k = 0;
        for (int j = 0; j < m; j++) {
            k += snprintf(hex + k, 4, "%02x ", b[i + j]);
            asc[j] = (b[i + j] >= 0x20 && b[i + j] < 0x7f) ? (char)b[i + j] : '.';
        }
        asc[m] = 0;
        printf("[C6 %6lldms +%3d] %-*s |%s|\n", (long long)t_ms, i, 3 * 32, hex, asc);
    }
    fflush(stdout);
}

void c6_flash_monitor_baud(int seconds, uint32_t baud)
{
    if (seconds <= 0) seconds = 10;
    if (baud < 9600) baud = 115200;
    ESP_LOGI(TAG, "监听 C6 UART (GPIO%d RX) %d 秒 @%u ...", C6_RX, seconds, (unsigned)baud);

    /* 兜底: 上一次 c6flash/唤醒扫描若没把 UART driver 释放掉, 这里先删再装。
     * 2026-09-29 实测: 开机自动 c6flash 之后 c6mon 恒定失败在
     * "uart: UART driver already installed" —— 于是 c6mon 从来没真正跑起来过。 */
    if (uart_driver_delete(C6_UART_NUM) == ESP_OK) {
        ESP_LOGW(TAG, "清掉了残留的 UART%d driver", C6_UART_NUM);
    }

    uart_config_t uc = {
        .baud_rate = (int)baud, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* 大 RX 缓冲: C6 若在 boot loop 会连续刷屏, 512B 的缓冲会被冲爆 -> 丢字节 ->
     * 收到的日志"缺字"(实测 "Build" 变 "Blid", "header" 变 "heaer") */
    if (uart_driver_install(C6_UART_NUM, 8192, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install 失败 (上一次 c6flash 没释放?)");
        return;
    }
    uart_param_config(C6_UART_NUM, &uc);
    uart_set_pin(C6_UART_NUM, C6_TX, C6_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    int64_t t0 = esp_timer_get_time();
    uint8_t buf[1024];
    int total = 0, bursts = 0;
    while ((esp_timer_get_time() - t0) / 1000000 < seconds) {
        int n = uart_read_bytes(C6_UART_NUM, buf, sizeof buf, pdMS_TO_TICKS(200));
        if (n > 0) {
            total += n; bursts++;
            dump_bytes(buf, n, (esp_timer_get_time() - t0) / 1000);
        }
    }
    int secs = (int)((esp_timer_get_time() - t0) / 1000000);
    uart_driver_delete(C6_UART_NUM);
    ESP_LOGI(TAG, "监听结束: %d 字节 / %d 批 / %ds (≈%d B/s)%s", total, bursts, secs,
             secs ? total / secs : 0,
             total ? "" : " —— C6 完全没说话: 查 pin9 是否接地 / TX-RX 是否接反 / C6 是否上电");
}

void c6_flash_monitor(int seconds)
{
    c6_flash_monitor_baud(seconds, 115200);
}

static void c6_flash_task(void *arg)
{
    long mode = (long)arg;
    bool use_stub = (mode & 1) != 0;
    uint32_t baud = (uint32_t)(mode >> 8);
    if (baud == 0) baud = 460800;
    c6_flash_run(use_stub, baud, 60);
    vTaskDelete(NULL);
}

#if CONFIG_VBCPET_C6_FLASH_ON_BOOT
static void c6_boot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));          /* 等显示/外设起来, 日志顺序好看 */
#if CONFIG_VBCPET_C6_WAKE_SCAN
    ESP_LOGW(TAG, "[开机自动] 先做 C6 唤醒扫描 (找它的 EN/复位脚)...");
    c6_wake_scan();
#endif
    ESP_LOGW(TAG, "[开机自动] 尝试给 C6 烧固件 (45s 内没反应就放弃)");
    c6_flash_run(true, 460800, 45);
    vTaskDelete(NULL);
}
#endif

void c6_flash_boot_hook(void)
{
#if CONFIG_VBCPET_C6_FLASH_ON_BOOT
    xTaskCreate(c6_boot_task, "c6-boot", 8192, NULL, 5, NULL);
#endif
}

void c6_flash_cmd(const char *sub, const char *rest)
{
    if (sub && !strcmp(sub, "mon")) {
        int secs = 10;
        uint32_t baud = 115200;
        if (rest && *rest) {
            secs = atoi(rest);
            const char *sp = strchr(rest, ' ');
            if (sp) { int v = atoi(sp + 1); if (v >= 9600) baud = (uint32_t)v; }
        }
        c6_flash_monitor_baud(secs, baud);
        return;
    }
    if (sub && !strcmp(sub, "scan")) {
        printf("c6scan: 开始扫候选脚 (GPIO54/53/52/0/49/50 × 高/低/高阻), 约 27s ...\n");
        c6_wake_scan();
        return;
    }
    if (sub && !strcmp(sub, "rom")) {
        xTaskCreate(c6_flash_task, "c6flash", 8192, (void *)(long)(0 | (115200 << 8)), 5, NULL);
        printf("c6flash: 纯 ROM 模式启动 (慢, 但最稳)\n");
        return;
    }
    uint32_t baud = 460800;
    if (sub && *sub) {
        int v = atoi(sub);
        if (v >= 115200) baud = (uint32_t)v;
    }
    xTaskCreate(c6_flash_task, "c6flash", 8192, (void *)(long)(1 | (baud << 8)), 5, NULL);
    printf("c6flash: 启动 (stub, %u baud, 等 60s) — 没进下载模式的话现在断电重上电\n", (unsigned)baud);
}

#else  /* !CONFIG_VBCPET_C6_FLASH */
int  c6_flash_run(bool use_stub, uint32_t baud, int wait_seconds) { (void)use_stub; (void)baud; (void)wait_seconds; return -1; }
void c6_flash_monitor(int seconds) { (void)seconds; }
void c6_flash_monitor_baud(int seconds, uint32_t baud) { (void)seconds; (void)baud; }
void c6_flash_boot_hook(void) {}
void c6_flash_cmd(const char *sub, const char *rest)
{
    (void)sub; (void)rest;
    printf("C6 烧录通道未编译 (CONFIG_VBCPET_C6_FLASH=n)\n");
}
#endif
