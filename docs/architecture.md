# 系统架构

**vbcpet 不只是一个桌宠，而是一套 AI Agent 的 Character Runtime。**
ESP32-P4 是它的第一种身体，Live2D 是它的第一张脸。未来可扩展到 VRM、手机、机器人等不同载体，
而 Character Event Protocol 与 Character Runtime 将保持一致。

## Character Runtime 核心架构

Character Runtime 是整个系统的核心——它接收来自 Agent、硬件、用户和环境的事件，
驱动角色状态，输出动作、表情、声音和身体语言。不单纯播放视频。

```
                    AI Agent (Claude Code / Codex / Cursor …)
                        │
                 Agent Event Stream
                        │
            ┌─── Character Runtime (PC) ───────┐
            │  • CharacterBrain (状态+行为)   │
            │  • Director (文本压缩, 可插拔)   │
            │  • Emotion Engine (情绪判定)    │
            │  • Risk Classifier (三层权限)   │
            │  • Live2D VideoTrack (渲染推流) │
            └──────────────┬────────────────┘
                           │
                   WebRTC (WiFi6)
        ┌──────────┬────────────┬──────────┐
        │  Video   │  Audio     │ DataChannel ×2  │
        │  H.264   │  Opus双向  │ ctrl(可靠) + realtime(不可靠) │
        └──────────┴────────────┴──────────┘
                           │
            ┌── ESP32-P4 Runtime ──────────┐
            │  • Sensor Manager (按键/ToF)  │
            │  • Servo Controller (DOF 0..N)│
            │  • Audio Codec (ES7210/8311)  │
            │  • Local Animation (fallback) │
            │  • RGB / Permission Indicator │
            └──────────────┬───────────────┘
                           │
                   BLE + WiFi Direct
                    ┌── 手机 App (可选) ──┐
                    │ • 高保真 Live2D     │
                    │ • Character State   │
                    └─────────────────────┘
```

## Character Event Protocol

所有事件—无论来自 Agent、硬件、用户还是环境—统一包装为 **CharacterEvent**：

```json
{"source": "agent|tool|hardware|user|environment",
 "type": "tool_start|tool_end|thinking|speech|emotion|touch|light|button|presence|...",
 "data": {...}, "seq": 123, "ts": 1700000000000}
```

Director（Character Runtime 的核心）**只消费 CharacterEvent**。
以后接新的 Agent（OpenAI Codex、Cursor、Aider、Gemini CLI）或新硬件（触摸、光线、摄像头），
只加事件类型，不改 Director 逻辑。

详见 [`decisions.md`](decisions.md) ADR-006。

## 双 DataChannel（控制 + 实时）

WebRTC 上两条 DataChannel（同一条 SCTP 连接）：

| Channel | Label | 模式 | 承载 |
|---|---|---|---|
| Control | `ctrl` | Reliable + Ordered | 工具授权、配置、状态同步、按键——**不能丢** |
| Realtime | `realtime` | Unreliable + Unordered | 表情、嘴型、眼睛、头部姿态——**可以丢** |

游戏同步经典方案：控制走可靠通道，动画走不可靠通道。详见 [`decisions.md`](decisions.md) ADR-007。

## 权限三层分级

| 风险等级 | 工具举例 | 行为 |
|---|---|---|
| **Low** | Read, Grep, Glob | 自动放行 |
| **Medium** | Write, Edit, Bash(safe) | 按键确认 (yes/no/yes-always) |
| **High** | Bash(rm), Write(覆盖关键文件) | 强制确认，RGB 变红 |

V1 启发式关键词匹配，V2 接导演 LLM 判定。详见 [`decisions.md`](decisions.md) ADR-008。

## 数据流

### 语音下行（用户说话 → 电脑执行 → 桌宠反馈）
1. 麦阵（ES7210 4ch，CH1 回采）→ P4 本地 VAD/打断 → Opus 编码 → WebRTC 音频轨上行到 PC。
2. PC 本地 **ASR**（Whisper）转文本 + **声纹识别**判定说话人。
3. 文本进入会话状态机；若是编程指令，转发给 **Agent 驱动**（stream-json / SDK）。
4. Agent 事件流（大段文本、工具调用、结果）经 **Character Runtime**：
   - Director 压缩 → `speech`（适合朗读的短文本）
   - Emotion Engine → `action` / `expression` / `emotion`（Live2D 标签）
   - Risk Classifier → 权限决策
5. `speech` → 本地 **TTS** → Opus → WebRTC 音频轨下行到 P4 播放。
6. `action/emotion/token/status` → **DataChannel** 下发（控制通道 + 实时通道分路）。

### 视频下行（Live2D 画面）
- PC 用真实 Live2D 引擎渲染角色（口型由 TTS 音频驱动）→ H.264 编码 → WebRTC 视频轨 → P4 **硬解**（VPU）→ MIPI-DSI 显示。
- 未来方向（ADR-006 衍生）：将嘴型/眼睛/呼吸从视频中拆出，走 Realtime DataChannel 做本地层叠，降低口型延迟。

### 工具授权回路
- Agent 请求工具 → Risk Classifier 判定等级 → 低风险自动放行 / 中高风险 `prompt_confirm` 经 DataChannel 下发
  → P4 **RGB 变色 + 按键等待** → 用户按 yes/no/yes-always → 上行 → `control_response` 回给 Agent。

## 为什么 PC 渲染 + 推流
ESP32-P4 无法原生跑 Live2D（需 OpenGL ES 2.0+ 可编程着色器做网格形变；P4 的 PPA 只是固定功能 2D 加速器）。
但 P4 自带 H.264 硬解（VPU）+ MIPI-DSI，天然适合做「视频接收显示终端」。详见 [`decisions.md`](decisions.md) ADR-001。

## 手机联动
- 手机作可选高保真显示端：手机 GPU 可跑原生/Web Live2D 高精度模型。
- **配对=BLE + 配套 App**；**在位检测=桌宠板上 ToF/微动开关**；**方向=手机重力感应**。
  检测 ≠ 配对，详见 [`decisions.md`](decisions.md) ADR-004。
- 手机与桌宠板共享同一套 **Character Event**——Character Runtime 对两者的指令协议一致。

## 未来载体 (Future Vision)

Character Runtime 不依赖任何具体 Agent 或具体硬件。同一套 Character Event 和 Character State 可以驱动：

| Agent | 载体 |
|---|---|
| Claude Code | Live2D (PC 渲染推流) |
| OpenAI Codex | VRM (手机/Web) |
| Cursor / Aider | ESP32-P4 桌宠板 |
| Gemini CLI | 机器人 (舵机 + 表情屏) |

所有前端共享同一套 Character Event Protocol 与 Character Runtime。
