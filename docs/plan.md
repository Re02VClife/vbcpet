# VibeCoding 桌宠 — 实施计划 (Milestone 1: 硬件 / PCB 优先)

> 本文件是已批准计划的仓库内副本，便于版本管理。原始副本位于用户 `.claude/plans/` 下。

## Context (背景与目标)

要做一个放在桌面的二次元 Live2D「桌宠」，作为电脑上 AI 代理（Claude Code / 小龙虾等）的**具身化交互终端**：

- 屏幕显示 Live2D 形象，可语音对话（参考小智），LLM 根据对话与工作状态驱动动作/表情（参考 Neuro-sama）。
- 可通过对话驱动电脑编程（Claude Code），并实时反馈工作状态；Claude 输出文本很大，需**总结/过滤/压缩**成适合语音的短文本再下发硬件。
- 对话支持**打断（barge-in）**、**声纹识别**；有 4 个快捷键（yes / no / yes-always / 面板）；有面板显示 token 用量等。
- 硬件参考小智，但因需运行 Live2D，选用更强的 **ESP32-P4 + ESP32-C6**，**板子自绘**（元件未选定）。
- 手机联动：设备兼作手机支架，手机竖放/横放到位时自动配对，把桌宠形象投到手机（手机 GPU 更强，可跑更高精度模型）。

**关键技术结论**：ESP32-P4 **无法原生跑 Live2D**（PPA 只是固定功能 2D 加速器，无着色器/网格形变），但自带 **H.264 硬解 (VPU 4K30)** + MIPI-DSI（800×480@60fps / 1024×600@~50fps）。

**已确认决策**（详见 [`decisions.md`](decisions.md)）：
1. Live2D：PC 渲染 → H.264 → 推流到 P4 硬解显示（ADR-001）。
2. 传输：WebRTC（视频轨 + 音频轨 + 双 DataChannel）（ADR-002, ADR-007）。
3. 语音/传输基座：fork xiaozhi 音频链路。
4. AI 后端：本地 ASR/TTS/声纹 + 可插拔导演/压缩 LLM（ADR-003）。
5. Character Event Protocol：所有事件统一 schema（ADR-006）。
6. 权限：Risk Classifier 三层分级（ADR-008）。
7. 手机检测 ≠ 配对（ADR-004）。
8. 首里程碑：硬件/PCB + Phase 0 devkit 去风险（ADR-005）。

## Milestone 1 交付物
1. 系统框图 + 电源树 + 完整 BOM（选型定稿）。 → [`hardware/bom.md`](../hardware/bom.md), [`hardware/power-tree.md`](../hardware/power-tree.md)
2. 原理图（KiCad，分 sheet）。 → [`hardware/schematic/`](../hardware/schematic/)
3. PCB 布局布线 + Gerber + 装配文件。 → [`hardware/pcb/`](../hardware/pcb/)
4. Phase 0 现成开发板去风险。 → [`hardware/phase0-validation.md`](../hardware/phase0-validation.md)

## 后续里程碑

- **M2** 固件（[`firmware/`](../firmware/)）—— ESP-IDF, fork 小智 + esp-webrtc。
  - 完成 I2C/I2S 音频 + Opus + AEC + VAD/打断。
  - DataChannel 协议固件端实现。
  - Wokwi P4 Beta 仿真验证。

- **M2.5** Character Runtime MVP —— **桌宠与普通 AI 终端的关键分水岭**。
  - Character Event Protocol 实现（`common/protocol.py` 升级）。
  - 双 DataChannel (`ctrl` + `realtime`) 落地。
  - Risk Classifier V1（启发式三层）。
  - `sim/chaos.py` Chaos Testing（`--loss --delay --disconnect`）。

- **M3** 硬件感知 + 身体开始"活起来"。
  - Touch / ToF 在位传感器驱动接入。
  - RGB LED 权限指示（Low=绿 / Medium=黄 / High=红）。
  - 双麦阵列 + 声纹识别接入 Character Runtime。

- **M3.5** 身体动作。
  - 头部舵机（DOF 0: Head Yaw, DOF 1: Head Pitch）。
  - Character Runtime 联动：emotion → DOF 映射。
  - 可选身体部件（DOF 2+: 耳朵/尾巴/手臂，兼容猫耳/狐狸/机器人）。

- **M4** Camera + Dock + Phone。
  - 摄像头：Presence 检测 → Face Direction → Hand Gesture（Future）。
  - 手机 App：BLE 配对 + 高保真 Live2D + 接收 CharacterEvent。
  - 手机停靠自动切换显示模式。

## 关键风险
- **Live2D 商用授权**：Cubism Core 闭源，商用需授权（PC 与手机端）。
- **推流延迟/带宽**：端到端体验成败关键，Phase 0 必须先量化。
- **P4 无原生 WiFi**：依赖 C6 + esp-hosted。
- **WebRTC 数据流方向**：P4 作接收/解码端，需专门验证（见 ADR-002）。

详细工程内容见各子目录文档。
