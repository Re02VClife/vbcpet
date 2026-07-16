# 架构决策记录 (ADR)

记录关键取舍，便于后续追溯。状态：Accepted = 已采纳。

---

## ADR-001 — Live2D 由 PC 渲染并推流，P4 只做硬解显示
**状态**：Accepted

**背景**：需求要 Neuro-sama 级别 Live2D 表现，硬件想放在 ESP32-P4。

**结论**：**PC 端跑真实 Live2D 引擎 → H.264 编码 → WebRTC 推流到 P4 硬解显示**。

**理由**：
- Live2D Cubism 运行时依赖 **OpenGL ES 2.0+ 可编程着色器**做网格（mesh）形变。
- ESP32-P4 的「GPU」是 **PPA（Pixel Processing Accelerator）**——固定功能 2D 加速器，只有缩放/旋转/镜像/Alpha 混合/填充，**无着色器、无三角形光栅化、无网格形变**，跑不了 Live2D。
- 但 P4 有 **H.264 硬件解码器（VPU，最高 4K30 解码）** + 2-lane MIPI-DSI（800×480@60fps / 1024×600@~50fps），天生适合当「视频显示终端」。
- 与需求「高开销流程放电脑/服务端处理完再传硬件」一致。

**否决的替代**：① 板上预烘焙动画片段（表现力受限、口型粗糙）；② 自研 PPA 分层贴图伪 Live2D 引擎（工作量大、非真 Live2D）。手机端因有真实 GPU，仍可跑原生高保真 Live2D。

---

## ADR-002 — 传输层用 WebRTC，替代小智默认的 WebSocket
**状态**：Accepted

**结论**：用 **WebRTC**（Espressif 官方 [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution)，P4 已支持）。单条连接承载：Live2D **H.264 视频轨** + 双向 **Opus 音频轨** + **SCTP DataChannel**（控制面 JSON）。

**理由**：
- WebSocket 走 TCP，有**队头阻塞**：一个丢包会拖住整条视频流 → 延迟尖刺，实时 Live2D 不可接受。
- WebRTC 走 UDP/SRTP，自带**抖动缓冲、NACK/FEC、拥塞控制与带宽自适应**，为实时音视频而生。
- 一条连接三路复用，工程更干净。
- 局域网内信令用本地 WHIP/HTTP，仅需 host ICE 候选，**无需 TURN**。

**代价/风险**：固件更复杂（ICE/DTLS/SRTP/SCTP），但 Espressif 已封装成组件，且有双向「视频通话」demo 可参考。**注意数据流方向**：官方 demo 多为 P4 采集→编码→发送；本项目 P4 是**接收+硬解视频**（`av_render`）+ 发麦克风 Opus，Phase 0 需专门验证接收/解码端。

仍保留小智（[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)）作**音频链路**基座（麦阵、ES7210/ES8311、Opus、AEC、VAD、打断、BSP），只替换其传输层。

---

## ADR-003 — 导演/压缩 LLM 用可插拔后端
**状态**：Accepted

**结论**：把大段 Claude Code 输出压成「短语音文本 + 动作/表情/情绪标签」的 LLM，采用**可插拔后端**：用户可配置接入 Claude / OpenAI / 兼容 OpenAI 的本地或第三方模型。统一适配层输出结构化 JSON。

**理由**：不同用户对成本/隐私/质量取舍不同；被控制的 AI 代理（Claude Code / 小龙虾）本身也是可替换的。ASR/TTS/声纹默认本地跑（隐私+低延迟）。

---

## ADR-004 — 手机检测 ≠ 配对；用非磁性在位传感器 + BLE 配对保证机型通用
**状态**：Accepted

**背景**：曾误以为「霍尔传感器与手机配对」。澄清如下。

**结论**：
- **配对（数据链路+身份）**：始终由 **BLE + 配套 App** 完成（一次配对长期记忆）。传感器只能**触发** App 切停靠显示模式，不能配对。
- **在位检测**：用**非磁性**传感器（竖/横槽各一个 **ToF/红外接近**或**微动开关**），任意手机通用。
- **方向（竖/横）**：App 内读**手机重力感应**，或由两个槽位传感器区分。
- 霍尔+磁铁**不通用**（仅 iPhone 12+/Qi2/MagSafe 有磁环，多数安卓无），仅作可选增强。
- NFC 仅作首配 tap-to-open 便利项（iOS 后台读卡受限），非配对核心。

---

## ADR-005 — 首个里程碑 = 硬件/PCB，但先用现成开发板做 Phase 0 去风险
**状态**：Accepted

**结论**：M1 聚焦硬件，但**自绘打板前**先用现成 ESP32-P4 开发板（如 ESP32-P4-Function-EV-Board）验证「DSI 显示 + WebRTC H.264 推流硬解 + 音频回路 + 按键」，量化延迟/带宽/内存后再定选型。

**理由**：推流延迟/带宽是端到端体验成败关键；在昂贵的自绘板打样前用现成板收敛不确定性，避免改板。

---

## ADR-006 — Character Event Protocol：所有事件统一走 CharacterEvent schema
**状态**：Accepted（来自升级方案第二条）

**背景**：当前 DataChannel 消息（`say`/`avatar`/`work_status`/`button`/`dock`/`vad` 等）各自有独立的 JSON 结构。不同来源（Agent / 硬件传感器 / 用户 / 环境）混在一起，Director 需要分别解析。

**结论**：引入统一的 **Character Event Protocol**。所有事件包装为：

```json
{"source": "agent|tool|hardware|user|environment",
 "type": "tool_start|tool_end|thinking|speech|emotion|touch|light|presence|button|...",
 "data": {...},
 "seq": 123, "ts": 1700000000000}
```

Director（Character Runtime）**只消费 CharacterEvent**。以后接 OpenAI Codex、Cursor、Aider 等新 Agent，或加触摸/光线/摄像头等新硬件，都只需新增事件类型——不改 Director 逻辑、不改传输层。

**代价**：对当前 `common/protocol.py` 做一次重构（加 `CharacterEvent` 封装 + `source` 字段），其他代码适配。工作量不大（≈半天），与现有消息向下兼容。

---

## ADR-007 — 双 DataChannel：控制通道（可靠）+ 实时通道（不可靠）
**状态**：Accepted（来自升级方案第三条）

**背景**：当前只有一个 DataChannel `ctrl`（reliable+ordered），动画/表情/嘴型/眼睛数据和工具授权/配置/状态同步混在一起。若网络抖动导致重传，动画卡顿直接影响角色"活感"。

**结论**：拆为两条 DataChannel：

| Channel | Label | 模式 | 承载 |
|---|---|---|---|
| Control | `ctrl` | Reliable + Ordered | 工具授权、配置、状态同步、按键——**不能丢** |
| Realtime | `realtime` | Unreliable + Unordered | 表情、嘴型、眼睛、头部姿态、手势——**可以丢** |

WebRTC 原生支持多条 DataChannel。一条 SCTP 连接上拆两条 label，不增加连接数。丢了动画帧下一帧自然刷新，不影响控制面。

**参考**：游戏同步经典方案——状态/控制走 TCP（可靠），动画/物理走 UDP（不可靠）。双 DataChannel 是 WebRTC 上等效做法。

---

## ADR-008 — 权限系统：Risk Classifier 三层分级
**状态**：Accepted（来自升级方案第五条）

**背景**：当前 `can_use_tool` → `prompt_confirm` → 按 yes/no/yes_always 是全量决策。实际场景中：大部分 `Read`/`Grep` 类只读操作不需要确认；高风险 `Bash`/`Write`/`Edit` 才需要。

**结论**：引入三级风险分类，在 `can_use_tool` 回调中先判定：

| 风险等级 | 工具举例 | 行为 |
|---|---|---|
| **Low** | Read, Glob, Grep, WebSearch | **自动放行**，不打断用户 |
| **Medium** | Write, Edit, Bash(echo/ls) | **按键确认**（yes/no/yes_always） |
| **High** | Bash(rm/git push), Write(覆盖关键文件) | **强制确认**，RGB 变红，需长按或二次确认 |

Risk Classifier 实现：
- V1（当前即可做）：启发式关键词匹配（"rm -rf" → High，"echo" → Medium，Read → Low）
- V2：调导演 LLM 判定（与 ADR-003 的可插拔后端复用同一 API）
- 目标：硬件端收到 `permission_required` + 风险等级时，**自动驱动对应 RGB 颜色和按键行为**（Low 绿、Medium 黄、High 红）
