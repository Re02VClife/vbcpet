# Phase 0 — 现成开发板去风险验证清单

**目的**：在自绘板打样前，用现成 ESP32-P4 开发板量化关键链路的延迟/带宽/内存，锁定选型，避免昂贵改板。

**结论门槛**：所有 P0 项通过且数据达标 → 冻结选型，进入原理图绘制。

---

## 0. 硬件准备
- [ ] 采购 ESP32-P4 开发板（优先带 MIPI-DSI 屏 + 音频 codec + C6）：
  - 候选 A：**ESP32-P4-Function-EV-Board**（7" 1024×600 DSI + codec + C6）
  - 候选 B：Waveshare ESP32-P4 系列 / DFRobot FireBeetle-2 ESP32-P4
- [ ] 记录板载 BSP 的引脚定义 → 回填 [`pinmap.md`](pinmap.md) 作参考。
- [ ] ESP-IDF v5.4+ 环境搭好，能编译烧录官方示例。

---

## P0-1 显示出图（MIPI-DSI）
- [ ] 跑通官方 LVGL/DSI 示例，屏正常显示。
- [ ] 测量满屏动画 **FPS**（目标：1024×600 ≥30fps；800×480 ≥50fps）。
- [ ] 记录 SRAM/PSRAM 占用。
- **达标**：目标分辨率下 FPS 与内存满足视频显示需求。

## P0-2 WebRTC 视频接收 + H.264 硬解（最高风险项）
> P4 作**接收/解码**端（与官方多数「采集→编码→发送」demo 方向相反），需重点验证。
- [ ] PC 侧起 WebRTC 发送端：读一段预渲染 Live2D H.264 → 推视频轨（用 esp-webrtc 配套/WHIP 或 gstreamer/浏览器）。
- [ ] P4 侧用 `esp_webrtc` + `av_render` 接收视频轨 → VPU 硬解 → DSI 显示。
- [ ] 测量 **端到端延迟**（PC 出帧→屏显示，目标 <200ms）。
- [ ] 测量 **码率/带宽** 与丢包下的表现（WiFi6 局域网）。
- [ ] 长跑稳定性（≥30 分钟无崩溃/内存泄漏），记录 free_psram 走势。
- **达标**：目标分辨率@≥24fps 下延迟<200ms、无明显卡顿、长跑稳定。
- **不达标预案**：降分辨率/帧率；或改「PC 渲染帧 + 局部/差分更新」；或调 GOP/码率/jitter buffer。

## P0-3 音频链路（复用小智方案）
- [ ] ES7210(4ch)+ES8311 录放跑通（I2C 配置 + I2S 数据）。
- [ ] **AEC** 回采（CH1 参考）有效：放音时上行麦克风回声被消除。
- [ ] **Opus** 编解码 + WebRTC 音频轨双向跑通。
- [ ] **VAD / 打断（barge-in）**：播放 TTS 时用户说话可打断。
- [ ] 测量音频**单向延迟**（目标 <150ms）。
- **达标**：双向清晰、AEC 有效、可打断、延迟可接受。

## P0-4 按键 + 工具授权回路
- [ ] 4 按键（yes/no/yes-always/panel）事件读取正常。
- [ ] 打通最小闭环：PC 侧 `claude -p --output-format stream-json --input-format stream-json --permission-prompt-tool stdio` 长驻 → 触发一个 `control_request` → 经 DataChannel `prompt_confirm` 下发 → 按键上报 → PC 回 `control_response`。
- **达标**：按 yes/no 能真实放行/拒绝一次工具调用。

## P0-5 C6 联网（esp-hosted）
- [ ] C6 跑 esp-hosted，P4 经 SDIO/SPI 联网成功。
- [ ] WiFi6 下与 PC 建立 WebRTC 连接（host ICE，无 TURN）。
- [ ] 记录 RSSI 与吞吐是否够 P0-2 的码率。
- **达标**：联网稳定，带宽满足视频轨。

## P0-6 综合闭环（可选加分）
- [ ] 语音说一句指令 → ASR → Claude Code 执行 → 导演压缩 → TTS 播报 + 面板显示 token → Live2D 视频播放。
- **达标**：端到端 demo 跑通（哪怕手动串联各环节）。

---

## 产出（写回仓库）
- [ ] `phase0-report.md`：各项实测数据（FPS、延迟、带宽、内存、功耗）。
- [ ] 据此**冻结**：屏分辨率/尺寸、目标码率/GOP、PA 选型、在位传感器选型、PD 档位、P4 模组/裸片。
- [ ] 回填 [`pinmap.md`](pinmap.md) / [`bom.md`](bom.md) / [`power-tree.md`](power-tree.md) 的 ⚠️ 项。
