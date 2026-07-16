# vbcpet — AI Agent 的 Character Runtime

**vbcpet 不只是一个桌宠，而是一套 AI Agent 的 Character Runtime。**
ESP32-P4 是它的第一种身体，Live2D 是它的第一张脸。未来可扩展到 VRM、手机、机器人等不同载体。

> **可语音对话、可通过对话驱动电脑编程并实时反馈工作状态。**
> Agent 事件 → Character Runtime（Director + Emotion + Risk Classifier）→ 动作/表情/声音/身体语言。
> 参考 Neuro-sama，语音链路参考小智。完整设计见 [`docs/plan.md`](docs/plan.md) 和 [`docs/architecture.md`](docs/architecture.md)。

## 核心架构：Character Runtime

```
AI Agent (Claude Code / Codex / Cursor …)
        ↓  Agent Event Stream
┌─ Character Runtime (PC) ──────────────┐
│  Director · Emotion · Risk Classifier │
│  Animation Scheduler                  │
└──────────────┬────────────────────────┘
               ↓  WebRTC (双 DataChannel + 视频轨 + 音频轨)
┌─ ESP32-P4 Runtime ────────────────────┐
│  Sensors · Servos · Audio · RGB       │
└──────────────┬────────────────────────┘
               ↓  BLE + WiFi Direct
        手机 App (可选高保真 Live2D)
```

- **Character Event Protocol** — 所有事件（Agent / 硬件 / 用户 / 环境）统一 schema（[ADR-006](docs/decisions.md)）
- **双 DataChannel** — `ctrl` 可靠（授权/配置）+ `realtime` 不可靠（表情/嘴型/眼睛）（[ADR-007](docs/decisions.md)）
- **权限三层分级** — Low 自动 / Medium 按键 / High 强制确认（[ADR-008](docs/decisions.md)）
- **可插拔 Agent** — fake / claude / claude-sdk，统一 `DriverCallbacks` 接口

## 仓库结构

```
vbcpet/
├─ docs/            # 计划、架构、决策记录(5×ADR)、WebRTC 协议规范
├─ hardware/        # KiCad 工程文档 + BOM/引脚/电源树/Phase0 验证清单 (M1)
├─ common/          # PC 侧共享模块: DataChannel 协议(Python) + 帧时间戳标记
├─ sim/             # ESP32-P4 软件替身 (WebRTC 对端, headless/GUI 双模式)
├─ firmware/        # ESP-IDF P4 工程 + Wokwi 仿真 + 协议 C 版 + 音频 codec (M2)
├─ pc-host/         # Agent 驱动 + 导演/压缩 LLM + WebRTC 推流 + 面板 (M3)
└─ phone/           # 手机 App (M4 待建)
```

## 快速开始

### 1. 装依赖

```powershell
cd C:\Users\XU\Desktop\vscode\vbcpet
python -m venv .venv
.\.venv\Scripts\pip.exe install -r requirements.txt
```

### 2. 启动仿真回环 (零成本，不依赖硬件)

**终端 1** — pc-host（默认假代理 + 启发式导演）：
```powershell
.\.venv\Scripts\python.exe pc-host\host.py --port 8080
```

**终端 2** — P4 软件替身（说一句话触发代理任务）：
```powershell
.\.venv\Scripts\python.exe sim\sim.py --url http://127.0.0.1:8080/whip --headless --duration 16 --say "帮我跑测试"
```

### 3. 真实 Claude Code（需 ccswitch / Claude CLI 可用，工具授权用 4 按键）

```powershell
# 终端 1 —— SDK 驱动 + 真实逐工具授权
.\.venv\Scripts\python.exe pc-host\host.py --port 8080 --driver claude-sdk -v

# 终端 2 —— 说一句需要工具的话，自动按 yes 或 no
.\.venv\Scripts\python.exe sim\sim.py --url http://127.0.0.1:8080/whip --headless --duration 50 --auto-key yes --say "帮我新建文件 hello.txt 内容 hi"
```

`--director` 选 `heuristic`（默认，零成本规则压缩）或 `openai`（需设环境变量 `VBCPET_OPENAI_BASE` / `VBCPET_OPENAI_KEY` / `VBCPET_OPENAI_MODEL`）。

## 已验证功能

| 功能 | 验证方式 | 状态 |
|---|---|---|
| WebRTC 连接 + DataChannel 双向 | `sim --headless` ↔ `pc-host` 回环 | ✅ 30FPS 视频轨，中位延迟 ~50ms |
| 假代理全回路（说→想→授权→按键→完成） | `--driver fake` + `--say` | ✅ 零成本验证 |
| 真实 Claude CLI 文本 + token 面板 | `--driver claude` | ✅ 真实用量 19.7k tokens |
| **SDK `can_use_tool` 按键授权** | `--driver claude-sdk` + `--auto-key yes/no` | ✅ deny 拦截、allow 放行均实测 |
| **导演/压缩 LLM** | `--director heuristic` | ✅ 压缩率可见 + 情绪检测 |
| 固件协议 C 版 | `firmware/main/protocol.{h,c}` (470行) | ✅ API 对齐 Python 版 |
| 音频 codec (小智方案) | `firmware/main/audio_codec.{h,c}` | ✅ ES7210/ES8311 初始化 |
| 硬件选型文档 | BOM / 引脚 / 电源树 / PCB 约束 | ✅ |

## 驱动选型

| `--driver` | 说明 | 成本 | 按键授权 |
|---|---|---|---|
| `fake` (默认) | 脚本假代理，完整事件流 | 零 | ✅ 逻辑通过 |
| `claude` | 真实 CLI 子进程 (stream-json) | API 费 | ⚠️ 文本/面板通，授权待 SDK |
| `claude-sdk` | SDK + `can_use_tool` 回调 | API 费 | ✅ deny/allow 实测通过 |
| `demo` | 早期纯演示循环 | 零 | - |

## 里程碑

| # | 内容 | 状态 |
|---|---|---|
| **M1** | 硬件 / PCB | 文档就绪，待买 P4 devkit 做 Phase 0 |
| **M2** | 固件 (ESP-IDF + Wokwi) | 代码骨架就绪，待 IDF 编译 |
| **M2.5** | **Character Runtime MVP** — Event Protocol + 双 DataChannel + Risk Classifier + Chaos Testing | 设计就绪 (ADR-006/007/008)，待实现 |
| **M3** | Touch / ToF / RGB / 双麦 + ASR/TTS | 部分代码就绪，待接 |
| **M3.5** | 头部舵机 + DOF 身体动作 | 待硬件 |
| **M4** | Camera + Phone App + Dock | 待建 |

## Future Vision

Character Runtime 不依赖任何具体 Agent 或具体硬件。同一套 Character Event 和 Character State 可以驱动：

| Agent | 载体 |
|---|---|
| Claude Code | Live2D (PC 渲染推流) |
| OpenAI Codex | VRM (手机/Web) |
| Cursor / Aider | ESP32-P4 桌宠板 |
| Gemini CLI | 机器人 (舵机 + 表情屏) |

## 参考

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — 语音/音频链路基座
- [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) — WebRTC 传输
- [claude-agent-sdk](https://pypi.org/project/claude-agent-sdk/) — Python Agent SDK (`can_use_tool` 权限回调)
