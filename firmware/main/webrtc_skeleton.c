/** WebRTC 接收链路骨架 — 后台推进, 板到后验证
 *
 * ⚠️ 本文件当前【不编译】(见 main/CMakeLists.txt 注释)。
 * 依赖 esp-hosted (C6 SDIO) + esp_webrtc, 板到前无法实测; 具体 API 以
 * 实际组件版本为准 (v5.5.3 + esp_webrtc x.y.z)。
 *
 * 链路: C6 WiFi (esp-hosted) → P4 网络栈 → esp_webrtc PeerConnection
 *       → H.264 RTP → VPU 硬解 → 帧缓冲 → DSI 屏
 * PC 侧 host.py / sim.py 已就绪可直接对接 (docs/architecture.md)。
 *
 * 板到后验证步骤见 docs/webrtc-bringup.md。
 */
#include <string.h>
#include "esp_log.h"

static const char *TAG = "webrtc-sk";

/* 1. esp-hosted: 使能 C6 WiFi 网络接口 (P4 侧 host 驱动)
 *
 *    // 依赖: espressif/esp_hosted 组件; C6 侧烧 esp-hosted slave 固件
 *    // 初始化后 P4 获得 wlan STA 接口, lwIP 可用
 *
 * 2. esp_webrtc: 建 PeerConnection
 *
 *    // 依赖: espressif/esp_webrtc (>=1.2), 需 sdkconfig 开 WebRTC
 *    // 1) 注册信令 (与 pc-host/host.py 的 WHIP/WHEP 信令对接)
 *    // 2) addTransceiver video/recvonly
 *    // 3) onTrack: 收 H.264 帧
 *
 * 3. H.264 VPU 硬解 → 帧缓冲 → DSI
 *
 *    // P4 VPU 驱动 (esp_driver_h264 / av_render), 输出 YUV → 转 RGB565
 *    // → display_pipeline 的 FB (复用 display_pipeline_get_fb + flush)
 *
 * 待板到后核对:
 *  - esp_webrtc 对 P4 的官方支持状态 / 版本
 *  - VPU H.264 解码驱动 API
 *  - esp-hosted C6 slave 固件预烧情况 (模组是否自带)
 *  - WiFi 吞吐 vs 需求 (文档: 50Mbps SDIO 余量)
 */
void webrtc_skeleton_note(void)
{
    ESP_LOGW(TAG, "WebRTC skeleton 未接入 — 见 docs/webrtc-bringup.md (板到后验证)");
}
