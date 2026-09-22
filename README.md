# vbcpet — AI Agent 的 Character Runtime

**vbcpet 不只是一个桌宠，而是一套 AI Agent 的 Character Runtime。**
ESP32-P4 是它的第一种身体，Live2D 是它的第一张脸。未来可扩展到 VRM、手机、机器人等不同载体。

> Agent 事件 → Character Runtime（Director + Emotion + Risk Classifier）→ 动作/表情/声音。
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

- **Character Event Protocol** — 所有事件统一 schema，5 事件源 × 16 事件类型（[ADR-006](docs/decisions.md)）
- **双 DataChannel** — `ctrl` 可靠（授权/配置）+ `realtime` 不可靠（表情/动作）（[ADR-007](docs/decisions.md)）
- **权限三层分级** — Low 自动 / Medium 按键 / High 强制确认 + RGB 指示（[ADR-008](docs/decisions.md)）
- **可插拔 Agent** — fake / claude / claude-sdk，统一 `DriverCallbacks` 接口
- **可插拔 Director** — heuristic（零成本规则）/ openai（兼容 API），文本压缩 + 情绪检测

## 仓库结构

```
vbcpet/
├─ docs/            # 架构、8×ADR 决策、协议规范、实施计划
├─ hardware/        # 硬件设计规划 — BOM/引脚/电源树/Phase0 验证清单 (M1)
├─ common/          # 共享模块 — CharacterEvent 协议 (Python) + 帧时间戳标记
├─ sim/             # P4 软件替身 — WebRTC 对端, headless/GUI 双模式 + 延迟测量
├─ firmware/        # ESP32-P4 固件 — ESP-IDF 骨架 + 协议 C 实现 + 音频接口 (M2)
├─ pc-host/         # PC 主机栈 — Agent 驱动 + Director + Risk + Live2D + WebRTC
├─ live2d/          # Live2D 渲染 — PixiJS v8 + Cubism 4, Hiyori 模型, 预览脚本
└─ phone/           # 手机 App (M4, 待建)
```

## 快速开始

### 1. 装依赖

```powershell
cd vbcpet
python -m venv .venv
.\.venv\Scripts\pip.exe install -r requirements.txt
cd live2d && npm install && cd ..
```

### 2. Live2D 预览（不依赖其他组件，先看效果）

```powershell
python live2d/preview.py
# → 浏览器打开 http://127.0.0.1:8765/render.html
```
![人设图](docs/assets/character-concept.jpg)
人设图（暂定）Q版形象适合小屏幕，后续若性能开销大可转换为像素风；
人设图转live2d格式的工作还没有做，先拿开源的跑测试
### 3. 仿真回环（零成本，不依赖硬件）

**终端 1** — pc-host：
```powershell
.\.venv\Scripts\python.exe pc-host\host.py --port 8080 --driver fake
```

**终端 2** — P4 软件替身：
```powershell
.\.venv\Scripts\python.exe sim\sim.py --url http://127.0.0.1:8080/whip --headless --say "帮我跑测试"
```

### 4. Live2D WebRTC 推流（需 Playwright）

```powershell
.\.venv\Scripts\python.exe pc-host\host.py --port 8080 --driver fake --live2d live2d/render.html
```

### 5. 真实 Claude Code

```powershell
# SDK 驱动 + 真实逐工具按键授权
.\.venv\Scripts\python.exe pc-host\host.py --port 8080 --driver claude-sdk -v
# 另开终端
.\.venv\Scripts\python.exe sim\sim.py --url http://127.0.0.1:8080/whip --headless --auto-key yes --say "新建文件 hello.txt"
```

`--director` 选 `heuristic`（默认，零成本规则压缩）或 `openai`（需设 `VBCPET_OPENAI_BASE/KEY/MODEL` 环境变量）。

## 已验证功能

| 功能 | 状态 |
|------|------|
| WebRTC 连接 + 双 DataChannel 双向 | ✅ ~30FPS 视频轨，中位延迟 ~50ms |
| CharacterEvent 协议 (Python + C 双实现) | ✅ v1 event + v0 flat 向下兼容 |
| 假代理全回路（说→想→授权→按键→完成） | ✅ |
| 真实 Claude CLI 文本 + token 面板 | ✅ 真实用量 19.7k tokens |
| **SDK `can_use_tool` 按键授权** | ✅ deny/allow 均实测通过 |
| Risk Classifier V1（三层关键词） | ✅ |
| Director（文本压缩 + 情绪检测） | ✅ heuristic + openai 双后端 |
| **Live2D Hiyori 模型渲染** | ✅ 480×800 竖屏, resolution 2x AA |
| **Live2D WebRTC 推流** | ✅ Playwright 无头截图 → H.264 视频轨 |
| 固件协议 C 实现 | ✅ API 对齐 Python 版，cJSON 编码/解析 |
| GUI 仿真 (Tkinter) | ✅ 视频显示 + 4 按键 + 在位模拟 |

## 驱动选型

| `--driver` | 说明 | 成本 | 按键授权 |
|---|---|---|---|
| `fake` (默认) | 脚本假代理，完整事件流 | 零 | ✅ 逻辑通过 |
| `claude` | 真实 CLI 子进程 (stream-json) | API 费 | ⚠️ control_request 协议 |
| `claude-sdk` | SDK + `can_use_tool` 回调 | API 费 | ✅ deny/allow + yes_always |

## 里程碑

| # | 内容 | 状态 |
|---|---|---|
| **M1** | 硬件 / PCB | P4 开发板已到，Phase 0 待启动 |
| **M2** | 固件 (ESP-IDF) | 协议 C 实现 + 音频接口就绪，WebRTC/DSI 待接 |
| **M2.5** | **Character Runtime MVP** | ✅ Event Protocol / 双通道 / Risk / SDK 授权 — 全部完成 |
| **M3** | Live2D + 导演 + 推流 | ✅ 渲染已验证，待 Playwright 安装做推流闭环 |
| **M3.5** | Touch / ToF / RGB / 舵机 | 待硬件 |
| **M4** | Camera + Phone App + Dock | 待建 |

## Future Vision

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
- [pixi-live2d-display](https://github.com/guansss/pixi-live2d-display) — PixiJS Live2D 插件
