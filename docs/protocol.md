# 通信协议规范 (v1 — Character Event Protocol)

定义 PC 主机 ↔ 桌宠板（ESP32-P4）之间的连接。传输为**单条 WebRTC 连接**，四路复用：

| 路 | 载体 | 方向 | 用途 |
|---|---|---|---|
| 视频轨 | H.264 (RTP) | PC → P4 | Live2D 画面，P4 硬解显示 |
| 音频轨 | Opus (RTP) | 双向 | 上行=麦克风；下行=TTS 语音 |
| 控制通道 | DataChannel `ctrl` (reliable+ordered) | 双向 | 工具授权、配置、状态同步——**不能丢** |
| 实时通道 | DataChannel `realtime` (unreliable+unordered) | PC → P4(主) | 表情/嘴型/眼睛/动作——**可以丢** |

> v1 = Character Event Protocol (ADR-006) + 双 DataChannel (ADR-007)。v0 flat 格式向下兼容。

## 0. 信令 (Signaling)
局域网内，PC 主机起本地 WHIP/HTTP 信令端点；P4 (或 sim) 作为 WebRTC 客户端发起 offer。
仅用 **host ICE 候选**，无需 STUN/TURN。两条 DataChannel 由 offer 方创建。

## 1. Character Event Protocol (v1, ADR-006)

所有事件统一包装为 **CharacterEvent**。Director（Character Runtime）只消费 character event。

```json
{
  "source": "agent|tool|hardware|user|environment",
  "type": "speech|emotion|status|panel|permission|tool_start|tool_end|button|presence|touch|user_input|vad|hello|heartbeat|speech_state",
  "data": { /* 事件类型特定的 payload */ },
  "seq": 123,
  "ts": 1700000000000
}
```

### 1.1 事件类型目录

#### PC → P4（下行）

| type | source | data 字段 | 通道 | 说明 |
|---|---|---|---|---|
| `speech` | agent | `text`, `utterance_id` | ctrl | Director 压缩后的语音文本 |
| `emotion` | agent | `action`, `emotion`, `intensity` | **realtime** | 表情/动作变更 |
| `status` | agent | `state`(thinking\|editing\|running\|blocked\|done\|error), `detail` | ctrl | 工作状态 |
| `panel` | agent | `tokens`, `cost_usd`, `session`, `model`, `turns` | ctrl | Token 用量面板 |
| `permission` | tool | `id`, `reason`, `options`, `risk_level`(0\|1\|2) | ctrl | 工具授权请求 |
| `speech_state` | agent | `state`(start\|end), `utterance_id` | ctrl | TTS 播放状态 |

#### P4 → PC（上行）

| type | source | data 字段 | 通道 | 说明 |
|---|---|---|---|---|
| `button` | user | `key`(yes\|no\|yes_always\|panel), `confirm_id` | ctrl | 用户按键 |
| `presence` | hardware | `slot`(portrait\|landscape), `present` | ctrl | 手机在位传感器 |
| `user_input` | user | `text` | ctrl | ASR 结果(sim 用其模拟说话) |
| `vad` | hardware | `state`(speech_start\|speech_end\|barge_in) | ctrl | 语音活动/打断 |
| `hello` | hardware | `fw`, `board`, `caps` | ctrl | 设备上线 + 能力声明 |
| `heartbeat` | hardware | `rssi`, `free_psram` | ctrl | 保活 |
| `touch` | hardware | `target` | ctrl | 触摸事件（Future） |

### 1.2 v0 flat 向下兼容

v0 消息 `{"t":"say","seq":1,"ts":…,"text":"…"}` 仍可被 `decode_event()` 识别，
映射为 `{"source":"agent","type":"speech","data":{"text":"…"},"seq":1,"ts":…}`。

## 2. 双 DataChannel 路由规则 (ADR-007)

| 通道 | Label | 参数 | 路由的消息 |
|---|---|---|---|
| Control | `ctrl` | ordered=true, maxRetransmits=-1 (可靠) | speech, status, panel, permission, button, presence, user_input, hello, heartbeat, vad, speech_state |
| Realtime | `realtime` | ordered=false, maxRetransmits=0 (不可靠) | **emotion** 及后续动画帧（mouth/eye/breath） |

实时通道丢帧不重传——丢了动画帧下一帧自然刷新，不影响控制面。

## 3. 权限三级风险 (ADR-008)

`permission` 事件的 `data.risk_level` 字段：

| risk_level | 含义 | P4 行为 |
|---|---|---|
| 0 (LOW) | 只读操作 (Read/Grep/Glob) | PC 侧自动放行，**不下发** permission 事件 |
| 1 (MEDIUM) | 写入/安全shell (Write/Edit/Bash safe) | 下发 permission，RGB **黄**，按键确认 |
| 2 (HIGH) | 高风险 (Bash rm/git push --force, Write 关键路径) | 下发 permission，RGB **红**，强制确认/长按 |

PC 侧 RiskClassifier (V1 启发式, `pc-host/risk.py`) 在 `can_use_tool` 回调前判定等级。
LOW 直接返回 `PermissionResultAllow`，MEDIUM/HIGH 经 DataChannel 请求用户按键。

## 4. 按键语义映射

| 按键 | 语义 | SDK `PermissionResult` |
|---|---|---|
| **yes** | 允许本次 | `PermissionResultAllow(updated_input=input)` |
| **no** | 拒绝本次 | `PermissionResultDeny(message="…")` |
| **yes_always** | 本会话始终允许该工具 | `Allow(updated_permissions=[addRules])` |
| **panel** | 切换面板显示 | 本地 UI，不回 Agent |

## 5. 手机 App 通道
手机与桌宠板共享同一套 CharacterEvent。App 收 `status`/`emotion`/`panel`/`speech` 事件驱动本地高保真 Live2D 渲染。
上位传感器 `presence` 事件触发 App 停靠模式切换。
