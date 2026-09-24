/** WiFi 链路 (模组内 ESP32-C6 + esp-hosted/SDIO) — W0 探测
 *
 * 背景: ESP32-P4 无原生 WiFi。模组 ESP32-P4C6-Core 内含一颗 ESP32-C6FH4,
 * 与 P4 之间是**模组内部 SDIO** 链路 (模组未引出的 GPIO14-19 = SDIO CLK/CMD/D0-D3),
 * C6 侧跑 esp-hosted slave 固件, P4 侧用 esp_hosted + esp_wifi_remote,
 * 之后就能用标准 esp_wifi API。
 *
 * ⚠️ 两个已知外部依赖 (W0 探测就是要验证它们):
 *   1. C6 是否预烧了**版本匹配**的 esp-hosted slave 固件 (不匹配则 host 侧初始化失败)
 *   2. 模组 pin2 = C6 的 ANT (IPEX 座) — 必须外插 2.4G 天线, 否则扫不到 AP
 */
#pragma once

#include <stdbool.h>

/** 初始化 esp_hosted 传输 + esp_wifi (STA)。0 = 成功 */
int  wifi_link_init(void);
/** 开机入口: init + (可选) 后台探测任务 */
int  wifi_link_boot(void);
/** CP 信息 (芯片/固件版本) + 扫描 AP, 打印到日志 */
int  wifi_link_probe(void);
/** 扫描 AP, 打印 SSID/RSSI/信道/加密 */
void wifi_link_scan(void);
/** 连接 AP (pass 可为 NULL) */
int  wifi_link_connect(const char *ssid, const char *pass);
/** 打印连接状态 + IP */
void wifi_link_status(void);
/** 控制台入口: wifi <scan|info|conn [ssid pass]|status|reconnect> */
void wifi_link_cmd(const char *sub, const char *rest);
