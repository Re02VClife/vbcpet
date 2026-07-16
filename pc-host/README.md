# pc-host (M3) — PC 主机栈

> 里程碑 M3，M1/M2 之后展开（部分模块 Phase 0 即可先起最小版）。运行在用户电脑，承担重计算。

## Phase 0 现状（已实现，可运行）
[`host.py`](host.py) 是最小骨架，与 [`../sim/`](../sim/) 做真实 WebRTC 回环，验证**传输 + 协议 + 代理驱动**：
- 本地 WHIP/HTTP 信令；向 sim 推占位视频轨（动画 + 帧时间戳标记）+ 占位音频轨。
- DataChannel `ctrl`：把「代理驱动」的事件转成 `work_status`/`say`/`avatar`/`panel`/`prompt_confirm` 下行；
  收 `user_text`（ASR 结果）转给代理；收 `button` 按 [`../docs/protocol.md`](../docs/protocol.md) §3 映射为授权结果。

### Agent 驱动（`--driver`）
被控制的 AI 代理经统一接口 [`agent_driver.py`](agent_driver.py) 抽象，三种实现：

| `--driver` | 实现 | 说明 |
|---|---|---|
| `fake` (默认) | [`fake_driver.py`](fake_driver.py) | 脚本化假代理，**无成本、不执行真实工具**，完整走「指令→状态→授权→按键→完成」闭环。开发/CI 首选。 |
| `claude` | [`claude_driver.py`](claude_driver.py) | 长驻真实 `claude` 子进程（headless 双向 stream-json）。**已验证**：`user_text`→真实回复(`say`)→真实 token/成本面板。 |
| `demo` | host.py 内脚本 | 早期纯演示循环（无代理）。 |

运行：
```bash
python -m venv .venv && .venv/Scripts/pip install -r requirements.txt   # 仓库根
.venv/Scripts/python pc-host/host.py --port 8080 --driver fake          # 假代理(推荐先跑)
.venv/Scripts/python pc-host/host.py --port 8080 --driver claude        # 真实 Claude Code
```
另开终端用 sim 说一句话触发：`python sim/sim.py --url http://127.0.0.1:8080/whip --headless --say "帮我跑一下测试"`。

### 工具授权（yes/no/yes_always）—— 重要结论
按 [claude-code-guide 调研]：真实**交互式逐工具授权**依赖 `control_request`/`control_response` 协议。
- 该协议在 **CLI v2.1.185 无 `--permission-prompt-tool` 标志**（已核实 `--help`），**原始 CLI 无法可靠触发**逐工具授权。
- 官方推荐路径是 **Python Agent SDK**（`claude-agent-sdk`，可装 v0.2.118）的 `can_use_tool` 回调——直接映射到 4 按键：
  `yes`→`PermissionResultAllow()`，`yes_always`→`Allow(updated_permissions=[addRules])`，`no`→`PermissionResultDeny(message)`。
- 现状：`claude_driver.py` 已按协议实现 control_response（含 `updatedInput` 镜像），一旦该协议可用即生效；
  **完整授权回路当前由 `fake` 驱动确定性验证通过**。真实授权回路 = 下一步接 SDK（`claude_sdk_driver.py`，待建）。

## 模块（M3 目标）
1. **Claude Code 驱动器**：headless 长驻进程
   `claude -p --output-format stream-json --input-format stream-json --verbose --permission-prompt-tool stdio`
   解析事件流（assistant/tool_use/result/control_request），维护会话。
2. **导演 / 压缩 LLM（可插拔后端）**：把大段 Claude 输出压成
   `{ speech, action, expression, emotion, work_status }` 结构化 JSON。
   后端适配层：Claude / OpenAI / OpenAI-兼容本地或第三方，配置切换（见 [`../docs/decisions.md`](../docs/decisions.md) ADR-003）。
3. **语音**：本地 **ASR**（Whisper）+ 本地 **TTS** + **声纹识别**（说话人判定）。
4. **Live2D 渲染 + 推流**：真实 Live2D 引擎渲染（口型由 TTS 驱动）→ H.264 编码 → WebRTC 视频轨。
5. **WebRTC 端**：视频轨发送 + 音频轨双向 + DataChannel `ctrl`（信令用本地 WHIP/HTTP）。
6. **面板 UI**：token 用量、成本、状态、会话信息。
7. **按键映射**：DataChannel `button` → Claude Code `control_response`（yes/no/yes_always，见 [`../docs/protocol.md`](../docs/protocol.md) §3）。

## 技术栈（待定）
- 语言：Python 或 Node.js（WebRTC 库：aiortc / node-webrtc / gstreamer）。
- Live2D：Cubism SDK（native）或 pixi-live2d（headless 浏览器）——注意**商用授权**。

## 参考
- Claude Code headless：`--output-format stream-json`、`--permission-prompt-tool stdio`、`control_request/response`。
- 协议细节见 [`../docs/protocol.md`](../docs/protocol.md)。
