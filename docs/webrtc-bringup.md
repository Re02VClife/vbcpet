# WebRTC 接收链路 — 板到后 bring-up 清单

> 状态: **后台推进 (2026-08-11)**。PC 侧 (pc-host/host.py + sim/sim.py + common/protocol.py) 已就绪;
> 固件侧只有骨架 (main/webrtc_skeleton.c, 未编译)。依赖 WiFi/C6, 板到前无法实测。

## 目标链路

```
PC: live2d_track.py (H.264) ──WHIP/WHEP 信令──▶ host.py
                                                │
ESP32-P4:  C6 WiFi (esp-hosted, SDIO) → 网络栈 → esp_webrtc PeerConnection
              → H.264 RTP 收流 → VPU 硬解 → YUV→RGB565 → display_pipeline → DSI 屏
```

## 依赖组件 (按需启用, 现注释在 main/idf_component.yml)

| 组件 | 作用 | 板到后核对项 |
|------|------|--------------|
| `espressif/esp_hosted` | C6 SDIO WiFi 桥 (P4 侧 host 驱动) | C6 slave 固件是否预烧在模组? 版本匹配? |
| `espressif/esp_webrtc` | WebRTC 栈 (信令/ICE/SRTP/H.264) | 对 ESP32-P4 的官方支持状态与版本 |

## 步骤

1. **启用依赖**: 在 `main/idf_component.yml` 取消 `esp_hosted` / `esp_webrtc` 注释,
   `idf.py reconfigure` 拉取组件; 若有版本/目标不兼容, 记录并反馈。

2. **esp-hosted 网络**:
   - C6 侧: 确认模组内置 C6 是否已烧 esp-hosted slave 固件 (鱼鹰出厂可能不带, 需单独烧)。
   - P4 侧: `esp_hosted_init` + STA 连接 → 验证 `ifconfig` / ping。
   - 文档依据: `hardware/expansion.md` 十节 (P4↔C6 内部 SDIO 互联)。

3. **esp_webrtc 最小 peer**:
   - 与 `pc-host/host.py` 的 WHIP/WHEP 信令对接 (参考 `sim/sim.py` 的 SDP offer/answer 流程)。
   - `addTransceiver("video", recvonly)`; `onTrack` 回调收 H.264 NAL。

4. **H.264 VPU 硬解**:
   - P4 VPU 驱动 (esp_driver_h264 或 av_render) 解码 → YUV 帧。
   - YUV → RGB565 转换 → 写入 `display_pipeline_get_fb()` → `display_pipeline_flush()`。
   - 目标: 端到端解码延迟 < 200ms (hardware/phase0-validation.md)。

5. **音频 (下行, 板到后可选)**:
   - MAX98357A 播放由 P4→C6→WiFi 转发的终端混音 (expansion.md 十节, 无蓝牙硬件)。

## 风险 / 待确认

- esp_webrtc 对 P4 的支持与版本 (最高风险项, 见 docs/plan.md Sprint 2)
- C6 esp-hosted slave 固件预烧状态
- VPU H.264 驱动 API 与 P4 rev 兼容性
- WiFi 实际吞吐 (文档标称 SDIO 50Mbps 余量 10×)

## 前提 (串口路径保底)

在 WebRTC 验证期间, 串口 JPEG 路径 (serial_bridge.py + DSI 后端) 保持可用 —
两条管线可并存, WebRTC 失败不影响桌面助手基本功能。
