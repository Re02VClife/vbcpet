/** WiFi 链路实现 — esp-hosted (C6) + esp_wifi, W0 探测版 */
#include "wifi_link.h"
#include "sdkconfig.h"
#include <stdio.h>

#if CONFIG_VBCPET_ENABLE_WIFI
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

static bool  s_inited;
static bool  s_got_ip;
static char  s_ip[20] = "-";
static char  s_ssid[33];

/* ── 事件: 关联/拿 IP ───────────────────────────────────────────── */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START%s", s_ssid[0] ? " → 自动连接" : "");
            if (s_ssid[0]) esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED (已关联 AP, 等 DHCP)");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
            s_got_ip = false;
            ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED reason=%d %s",
                     e ? (int)e->reason : -1,
                     (e && e->reason == WIFI_REASON_AUTH_FAIL) ? "(密码错?)" : "");
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
            break;
        default:
            ESP_LOGI(TAG, "WIFI_EVENT id=%d", (int)id);
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_got_ip = true;
        ESP_LOGI(TAG, "*** GOT_IP %s  (WiFi 链路可用) ***", s_ip);
    }
}

/* ── CP 信息 (C6 芯片 + slave 固件版本) ─────────────────────────── */
static void cp_info(void)
{
    /* host 侧 auto-init 可能与 app 并发, 重试几轮 */
    for (int i = 0; i < 10; i++) {
        uint32_t chip_id = 0;
        char name[32] = {0};
        if (esp_hosted_get_cp_info(&chip_id, name, sizeof name) == 0) {
            ESP_LOGI(TAG, "CP 芯片: id=0x%08X  target=\"%s\"", (unsigned)chip_id, name);
            esp_hosted_coprocessor_fwver_t v = {0};
            if (esp_hosted_get_coprocessor_fwversion(&v) == 0) {
                ESP_LOGI(TAG, "CP slave 固件: %u.%u.%u rev=%d",
                         (unsigned)v.major1, (unsigned)v.minor1, (unsigned)v.patch1, (int)v.revision);
            } else {
                ESP_LOGW(TAG, "取 CP 固件版本失败 —— slave 固件可能过旧/不匹配");
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGE(TAG, "读 CP 信息失败 (SDIO/esp-hosted 传输没起来) —— "
                  "查: ① C6 是否烧了 esp-hosted slave 固件 ② SDIO 引脚(GPIO14-19 内部链路) ③ 模组是否处于正常启动");
}

/* ── 初始化 ─────────────────────────────────────────────────────── */
int wifi_link_init(void)
{
    if (s_inited) return 0;
    esp_err_t e;

    /* ── 1) 先拉 P4↔C6 的 esp-hosted/SDIO 链路, 并**验证它真的通** ──
     * ⚠️ 顺序是血的教训: 链路没通就调 esp_wifi_* , 会在 WiFi-RPC 的空队列上
     *    assert (xQueueSemaphoreTake NULL) → panic → 重启循环 (屏幕一亮一灭)。
     *    所以这里失败就直接返回, 一行 WiFi API 都不碰。 */
    int hrc = esp_hosted_init();
    if (hrc != 0) {
        ESP_LOGE(TAG, "esp_hosted_init 失败 rc=%d —— C6(SDIO) 链路没起来, 跳过 WiFi 初始化", hrc);
        printf("wifi: C6 链路未建立 (esp_hosted_init rc=%d)\n", hrc);
        return -1;
    }
    esp_hosted_connect_to_slave();

    uint32_t cp_id = 0;
    char cp_name[32] = {0};
    bool link_ok = false;
    for (int i = 0; i < 10; i++) {
        if (esp_hosted_get_cp_info(&cp_id, cp_name, sizeof cp_name) == 0) { link_ok = true; break; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!link_ok) {
        ESP_LOGE(TAG, "★ C6 无响应 —— 模组里的 ESP32-C6 大概率**没有 esp-hosted slave 固件**(或版本不匹配)。");
        ESP_LOGE(TAG, "  处理: 模组 pin4(C6_U0RXD)/pin5(C6_U0TXD) 接 USB-TTL, pin9(C6_IO9)→GND,");
        ESP_LOGE(TAG, "        断电重上电进 C6 下载模式, 烧 firmware_c6 的固件 (见 docs/wifi-camera-bringup.md)");
        printf("wifi: C6 无响应 → 需要先给 C6 烧 esp-hosted slave 固件\n");
        return -1;                      /* 关键: 不继续, 避免 RPC 空队列 assert */
    }
    ESP_LOGI(TAG, "P4↔C6 链路 OK: CP id=0x%08X target=\"%s\"", (unsigned)cp_id, cp_name);

    /* ── 2) 链路确认后再动 WiFi ── */
    e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "esp_netif_init=%d", e);
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "event_loop=%d", e);
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) esp_netif_create_default_wifi_sta();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL);

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&ic)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败 err=%d (hosted 传输没起来?)", e);
        return -1;
    }
    if (CONFIG_VBCPET_WIFI_SSID[0]) {
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid, CONFIG_VBCPET_WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
        strncpy((char *)cfg.sta.password, CONFIG_VBCPET_WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        strncpy(s_ssid, CONFIG_VBCPET_WIFI_SSID, sizeof(s_ssid) - 1);
        ESP_LOGI(TAG, "默认 SSID = \"%s\"", s_ssid);
    }
    if ((e = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) { ESP_LOGE(TAG, "set_mode=%d", e); return -1; }
    if ((e = esp_wifi_start()) != ESP_OK)                 { ESP_LOGE(TAG, "start=%d", e);    return -1; }

    s_inited = true;
    ESP_LOGI(TAG, "esp_wifi 已启动 (transport = esp_hosted/SDIO → C6)");
    return 0;
}

void wifi_link_scan(void)
{
    if (!s_inited && wifi_link_init() != 0) return;
    wifi_scan_config_t sc = {0};
    esp_err_t e = esp_wifi_scan_start(&sc, true);   /* true = 阻塞到扫完 */
    if (e != ESP_OK) { ESP_LOGE(TAG, "scan_start err=%d", e); return; }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    ESP_LOGI(TAG, "扫描完成: %u 个 AP", (unsigned)n);
    if (!n) {
        ESP_LOGW(TAG, "一个 AP 都没有 —— 先确认模组天线 (pin2 LAN_OUT / IPEX 座) 是否插了 2.4G 天线!");
        return;
    }
    uint16_t m = (n > 30) ? 30 : n;
    wifi_ap_record_t *recs = calloc(m, sizeof(wifi_ap_record_t));
    if (!recs) return;
    if (esp_wifi_scan_get_ap_records(&m, recs) == ESP_OK) {
        for (int i = 0; i < m; i++) {
            ESP_LOGI(TAG, "  %2d) %-32s RSSI %4d dBm  ch %2d  auth %d",
                     i + 1, (const char *)recs[i].ssid, (int)recs[i].rssi,
                     (int)recs[i].primary, (int)recs[i].authmode);
        }
    }
    free(recs);
}

int wifi_link_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) { ESP_LOGE(TAG, "缺少 SSID"); return -1; }
    if (!s_inited && wifi_link_init() != 0) return -1;
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass) strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "set_config err=%d", e); return -1; }
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    ESP_LOGI(TAG, "连接 \"%s\" ...", ssid);
    return (esp_wifi_connect() == ESP_OK) ? 0 : -1;
}

void wifi_link_status(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(TAG, "状态: 已关联 \"%s\"  RSSI %d dBm  ch %d %s",
                 (const char *)ap.ssid, (int)ap.rssi, (int)ap.primary, s_got_ip ? "" : "(等 IP)");
        printf("wifi: 已关联 %s  RSSI %d dBm  IP=%s\n", (const char *)ap.ssid, (int)ap.rssi, s_ip);
    } else {
        ESP_LOGW(TAG, "状态: 未连接 (SSID 目标=\"%s\")", s_ssid);
        printf("wifi: 未连接  IP=%s\n", s_ip);
    }
}

int wifi_link_probe(void)
{
    ESP_LOGI(TAG, "=== WiFi 探测 (C6 esp-hosted + 扫描) ===");
    if (wifi_link_init() != 0) {
        ESP_LOGE(TAG, "init 失败 —— 传输层没起来, 后面都别试了");
        return -1;
    }
    cp_info();
    if (CONFIG_VBCPET_WIFI_SSID[0]) {
        /* 配了 SSID: 已自动连接, 等 15s 看是否拿到 IP; 拿到就不扫描 (扫描会短暂打断关联) */
        for (int i = 0; i < 30 && !s_got_ip; i++) vTaskDelay(pdMS_TO_TICKS(500));
        if (s_got_ip) {
            ESP_LOGI(TAG, "✅ WiFi 已连通: SSID=\"%s\" IP=%s (下一步可上 TCP 推流)", s_ssid, s_ip);
            printf("wifi: 已连通 %s  IP=%s\n", s_ssid, s_ip);
            return 0;
        }
        ESP_LOGW(TAG, "配了 SSID 但 15s 没拿到 IP —— 看上面的 DISCONNECTED reason; "
                      "常见: 密码错(15) / 信号太弱(天线没插) / AP 是 5G-only");
    }
    wifi_link_scan();
    return 0;
}

void wifi_link_cmd(const char *sub, const char *rest)
{
    if (!sub || !sub[0] || !strcmp(sub, "status")) { wifi_link_status(); return; }
    if (!strcmp(sub, "scan"))  { wifi_link_scan();  return; }
    if (!strcmp(sub, "info"))  { wifi_link_probe(); return; }
    if (!strcmp(sub, "init"))  { printf(wifi_link_init() == 0 ? "wifi: 链路 OK\n" : "wifi: 链路失败\n"); return; }
    if (!strcmp(sub, "probe")) { wifi_link_probe(); return; }
    if (!strcmp(sub, "reconnect")) { esp_wifi_connect(); printf("wifi: 重连中\n"); return; }
    if (!strcmp(sub, "conn") || !strcmp(sub, "connect")) {
        char ssid[33] = {0}, pass[65] = {0};
        if (rest && *rest) {
            const char *p = rest;
            while (*p == ' ') p++;
            int i = 0;
            while (*p && *p != ' ' && i < 32) ssid[i++] = *p++;
            while (*p == ' ') p++;
            i = 0;
            while (*p && *p != ' ' && i < 64) pass[i++] = *p++;
        }
        if (!ssid[0]) {                                   /* 回落到 Kconfig 缺省 */
            strncpy(ssid, CONFIG_VBCPET_WIFI_SSID, sizeof(ssid) - 1);
            strncpy(pass, CONFIG_VBCPET_WIFI_PASSWORD, sizeof(pass) - 1);
        }
        if (!ssid[0]) { printf("用法: wifi conn <ssid> <password>\n"); return; }
        wifi_link_connect(ssid, pass);
        return;
    }
    printf("用法: wifi <scan|info|conn [ssid password]|status|reconnect>\n");
}

/* 开机探测任务 (Kconfig 开关) */
#if CONFIG_VBCPET_WIFI_PROBE_ON_BOOT
static void wifi_probe_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));       /* 等显示/外设起来, 看清日志顺序 */
    wifi_link_probe();
    vTaskDelete(NULL);
}
#endif

int wifi_link_boot(void)
{
    /* 开机**不**主动初始化 esp-hosted/WiFi:
     * C6 没有 slave 固件时, 过早碰这条链路风险高 (而且现在首要是把 C6 刷好)。
     * 一律由控制台命令触发: wifi info/scan/conn —— 那时链路是死是活一目了然。 */
    ESP_LOGI(TAG, "WiFi 待命: 需要时输入  wifi info | wifi scan | wifi conn <ssid> <pw>");
    return 0;
}

#if CONFIG_VBCPET_WIFI_PROBE_ON_BOOT
/* 保留原自动探测任务, 但只在被显式调用时创建 (见命令 wifi probe) */
void wifi_link_probe_task_start(void)
{
    xTaskCreate(wifi_probe_task, "wifi-probe", 6144, NULL, 4, NULL);
}
#endif

#else  /* !CONFIG_VBCPET_ENABLE_WIFI — 空实现, 保证调用点无需 ifdef */
int  wifi_link_init(void) { return -1; }
int  wifi_link_boot(void) { return -1; }
int  wifi_link_probe(void) { return -1; }
void wifi_link_scan(void) {}
int  wifi_link_connect(const char *s, const char *p) { (void)s; (void)p; return -1; }
void wifi_link_status(void) {}
void wifi_link_cmd(const char *s, const char *r) { (void)s; (void)r; printf("WiFi 未编译 (CONFIG_VBCPET_ENABLE_WIFI=n)\n"); }
#endif
