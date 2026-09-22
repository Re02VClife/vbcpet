# pc-host — PC 主机栈

PC 侧重计算引擎。WebRTC 信令 + Agent 驱动 + Director + Risk Classifier + Live2D 推流。

## 模块总览

| 文件 | 职责 | 状态 |
|------|------|------|
| `host.py` | 主入口：WHIP 信令、WebRTC、双 DataChannel 路由、驱动/导演/Live2D 编排 | ✅ |
| `agent_driver.py` | 抽象基类：`AgentDriver` + `DriverCallbacks` + `PermissionRequest` + `Usage` | ✅ |
| `fake_driver.py` | 脚本化假代理，完整事件流（think→edit→blocked→run→done），零成本测试 | ✅ |
| `claude_driver.py` | Claude CLI 子进程驱动（stream-json 双向），真实 token/文本 | ✅ |
| `claude_sdk_driver.py` | Claude Agent SDK 驱动（`can_use_tool` → 4 按键授权），**推荐** | ✅ |
| `director.py` | Director：文本压缩 + 情绪/动作标签，可插拔后端 | ✅ |
| `director_backends.py` | `HeuristicBackend`（零成本规则）+ `OpenAIBackend`（兼容 API） | ✅ |
| `risk.py` | Risk Classifier V1：三层关键词匹配（Low/Medium/High） | ✅ |
| `live2d_track.py` | Playwright 无头 Chromium → Live2D 截图 → H.264 VideoTrack | ✅ |

## 运行

### 依赖

```bash
pip install -r requirements.txt        # aiortc, aiohttp, numpy, Pillow
pip install playwright                 # 仅 --live2d 模式
pip install claude-agent-sdk           # 仅 --driver claude-sdk
```

### 启动方式

```bash
# 假代理（默认，零成本测试）
python pc-host/host.py --port 8080

# 真实 Claude SDK + 按键授权
python pc-host/host.py --port 8080 --driver claude-sdk -v

# 真实 Claude CLI
python pc-host/host.py --port 8080 --driver claude -v

# Live2D 推流模式（需 Playwright + npm install）
python pc-host/host.py --port 8080 --live2d live2d/render.html

# OpenAI Director（需环境变量）
$env:VBCPET_OPENAI_BASE="https://api.deepseek.com/v1"
$env:VBCPET_OPENAI_KEY="sk-..."
python pc-host/host.py --port 8080 --director openai
```

### 配合 sim 测试

```bash
# 终端 1
python pc-host/host.py --port 8080 --driver fake

# 终端 2
python sim/sim.py --url http://127.0.0.1:8080/whip --headless --say "帮我跑测试"
```

## Agent 驱动（`--driver`）

| `--driver` | 实现 | 授权方式 | 成本 |
|---|---|---|---|
| `fake` (默认) | 脚本化假代理 | 假授权回路（Future 解析） | 零 |
| `claude` | CLI 子进程 stream-json | `control_request`/`control_response` | API 费 |
| `claude-sdk` | Python Agent SDK `can_use_tool` | **4 按键直连** → `Allow`/`Deny`/`addRules` | API 费 |
| `demo` | host.py 内置演示循环 | - | 零 |

所有驱动通过 `agent_driver.AgentDriver` 抽象基类统一接口。

## Director（`--director`）

| `--director` | 说明 | 成本 |
|---|---|---|
| `heuristic` (默认) | 正则清洗 + 首句提取 + 关键词情绪检测 | 零 |
| `openai` | 调用 `/v1/chat/completions`，LLM 压缩 + 情绪标签 | API 费 |

`openai` 后端配置通过环境变量：
- `VBCPET_OPENAI_BASE` — API 地址（如 `https://api.deepseek.com/v1`）
- `VBCPET_OPENAI_KEY` — API Key
- `VBCPET_OPENAI_MODEL` — 模型名（默认 `deepseek-chat`）

## Risk Classifier（三层权限）

`risk.classify(tool_name, tool_input) → (RiskLevel, reason)`

| 等级 | 触发条件 | 行为 |
|------|---------|------|
| **Low** | Read/Grep/Glob/WebSearch + 安全 Bash | SDK 自动 `Allow` |
| **Medium** | Write/Edit/一般 Bash | DataChannel `prompt_confirm` → 等按键 |
| **High** | rm -rf/sudo/curl\|sh/git push -f/关键路径 | DataChannel + RGB 红 |

## Live2D 推流

`live2d_track.Live2DVideoTrack`:
1. 起本地 HTTP 服务器（绕过 CORS）
2. Playwright 无头 Chromium 加载 `render.html`
3. 等待 `window._ready`
4. 每帧截图 → PIL → numpy → PyAV → VideoFrame
5. `apply_avatar(action, emotion, intensity)` 驱动动作/表情

`ACTION_TO_MOTION` / `EMOTION_TO_EXPRESSION` 映射 Director 输出到 Live2D 标准名称。

### 独立串口推流脚本（不含 Agent）

上面的 WebRTC 路线是 M3 目标。当前 V1 板实际在跑的是**串口直推**——屏幕挂在 UART0/CH340 上：

| 文件 | 说明 |
|------|------|
| `stream_final.py` | ★ 当前方案：`FRAME:<hexsize>\n` + JPEG → COM@2M，同时把固件日志按 `[FW]` 前缀回显 |
| `serial_bridge.py` | 推流 + Claude 交互 + 表情/心情 + 按键模拟的单进程主控（v0.2） |

```powershell
.venv/Scripts/python pc-host/stream_final.py --port COM8 --baud 2000000 --quality 55
```

## 协议

- **Downlink**: `speech` / `emotion`(→realtime) / `status` / `panel` / `permission`
- **Uplink**: `user_text`(→Agent) / `button`(→授权) / `hello` / `heartbeat`
- 详见 [`../docs/protocol.md`](../docs/protocol.md) 和 [`../common/protocol.py`](../common/protocol.py)
