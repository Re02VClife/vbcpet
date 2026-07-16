# vbcpet — 实施计划

> 由 M1 硬件优先调整为新顺序：软件先行（Character Runtime 闭环），硬件验证并行。

## 当前状态（2026-07-16）

**PC 侧 80% 就绪**：Agent 驱动（fake/claude/claude-sdk）、Director（heuristic/openai）、
Risk Classifier V1、Live2D 渲染 + 预览、WebRTC 双 DataChannel、P4 软件仿真器均已实现。

**ESP32-P4 开发板已到**，Phase 0 去风险验证即将开始。

详见 [`docs/decisions.md`](decisions.md) 中的 8 个 ADR 和 [`../升级方案2.md`](../升级方案2.md)。

## 已验证组件

| 组件 | 状态 |
|------|------|
| CharacterEvent Protocol (Python + C 双实现) | ✅ |
| 双 DataChannel (ctrl + realtime) | ✅ |
| Agent 驱动 ×3 (fake/claude/claude-sdk) | ✅ |
| Director ×2 (heuristic/openai) | ✅ |
| Risk Classifier V1 | ✅ |
| Live2D Hiyori 渲染 (PixiJS v8 + Cubism 4) | ✅ |
| Live2D 预览脚本 + WebRTC 视频轨 | ✅ |
| P4 软件仿真器 (headless + GUI) | ✅ |
| 固件协议 C 实现 | ✅ |
| 音频 codec 接口 (ES7210+ES8311) | ✅ 接口定义，实现占位 |

## 优先级排序

### Sprint 1 — Character Brain（在 P4 验证的同时做）

目标：让 Live2D 角色从"播放器"升级为"具身化角色"。

- [ ] `common/character.py`：CharacterState + CharacterIntent
- [ ] 升级 Director → CharacterBrain（包裹现有 Director，叠加状态层）
- [ ] Live2D `setParameter()` API：眼睛、嘴、头部参数实时控制
- [ ] 闭环：Agent 事件 → CharacterBrain → 连续参数变化 → Live2D 自然表现

### Sprint 2 — P4 硬件验证（并行）

- [ ] DSI 屏幕点亮（官方 LVGL 示例）
- [ ] WebRTC H.264 接收解码（esp-webrtc + VPU）
- [ ] WiFi6 带宽/延迟测量
- [ ] 音频回路（ES7210 + ES8311 + Opus）

### Sprint 3 — Touch + 身体

- [ ] GT911 触摸 → `touch` 事件
- [ ] ToF 在位 → `presence` 事件
- [ ] 舵机头部（DOF Yaw/Pitch）
- [ ] RGB LED 权限指示

### Sprint 4 — 产品化

- [ ] Camera（本地 only，presence + face direction）
- [ ] Phone App 停靠模式
- [ ] KiCad 自绘板（原理图 → PCB → 打样）

## 关键风险

| 风险 | 缓解 |
|------|------|
| Live2D Cubism Core 商用授权 | PC/手机端均需授权 |
| P4 WebRTC 接收解码延迟 | Phase 0 量化，必要时降分辨率/帧率 |
| P4 无原生 WiFi | C6 + esp-hosted |
| Motion 文件占位 | 需从 Live2D SDK 获取真实文件 |
