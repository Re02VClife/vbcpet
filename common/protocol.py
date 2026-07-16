"""DataChannel 控制协议 + Character Event Protocol (v1)。

对应 docs/protocol.md 和 docs/decisions.md ADR-006。
消息为单行 JSON。两层兼容：

  v0 (flat):   {"t": "say", "seq": 1, "ts": …, "text": "…"}
  v1 (event):  {"source": "agent", "type": "speech", "data": {…}, "seq": 1, "ts": …}

decode_event() 自动识别两层。encode() / m_*() 保持向下兼容。
event_encode() 和 event_*() 为新 CharacterEvent 层。
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from itertools import count
from typing import Any

CTRL_CHANNEL = "ctrl"          # 控制 DataChannel (reliable)
REALTIME_CHANNEL = "realtime"  # 实时 DataChannel (unreliable, ADR-007)

# ---- 消息类型常量 (v0 flat, 保留向下兼容) ----
# PC -> P4 (下行)
T_SPEECH_STATE = "speech_state"
T_SAY = "say"
T_AVATAR = "avatar"
T_WORK_STATUS = "work_status"
T_PANEL = "panel"
T_PROMPT_CONFIRM = "prompt_confirm"
# P4 -> PC (上行)
T_BUTTON = "button"
T_DOCK = "dock"
T_VAD = "vad"
T_USER_TEXT = "user_text"
T_HELLO = "hello"
T_HEARTBEAT = "heartbeat"

# ---- Character Event 事件源 (v1, ADR-006) ----
SOURCE_AGENT = "agent"
SOURCE_TOOL = "tool"
SOURCE_HARDWARE = "hardware"
SOURCE_USER = "user"
SOURCE_ENVIRONMENT = "environment"

# ---- Character Event 事件类型 (v1) ----
EVT_SPEECH = "speech"             # Agent 输出 / Director 压缩后的语音文本
EVT_EMOTION = "emotion"           # 表情/动作/情绪变更
EVT_STATUS = "status"             # 工作状态变更 (thinking/running/done/error)
EVT_PANEL = "panel"               # Tokens / 成本 / 会话信息
EVT_PERMISSION = "permission"     # 工具授权请求
EVT_TOOL_START = "tool_start"     # 工具开始执行
EVT_TOOL_END = "tool_end"         # 工具执行完成
EVT_BUTTON = "button"             # 用户按键
EVT_PRESENCE = "presence"         # 传感器: 手机在位 / 物体接近
EVT_TOUCH = "touch"               # 传感器: 触摸
EVT_USER_INPUT = "user_input"     # 用户文本输入 (ASR 结果)
EVT_VAD = "vad"                   # 语音活动检测
EVT_HELLO = "hello"               # 设备上线
EVT_HEARTBEAT = "heartbeat"       # 保活
EVT_LIGHT = "light"               # 环境: 光照强度
EVT_SPEECH_STATE = "speech_state" # TTS 播放开始/结束

# Event type -> source 映射（构造时自动填 source）
_EVT_SOURCE: dict[str, str] = {
    EVT_SPEECH: SOURCE_AGENT, EVT_EMOTION: SOURCE_AGENT,
    EVT_STATUS: SOURCE_AGENT, EVT_PANEL: SOURCE_AGENT,
    EVT_PERMISSION: SOURCE_TOOL, EVT_TOOL_START: SOURCE_TOOL, EVT_TOOL_END: SOURCE_TOOL,
    EVT_BUTTON: SOURCE_USER, EVT_PRESENCE: SOURCE_HARDWARE,
    EVT_TOUCH: SOURCE_HARDWARE, EVT_USER_INPUT: SOURCE_USER,
    EVT_VAD: SOURCE_HARDWARE, EVT_HELLO: SOURCE_HARDWARE,
    EVT_HEARTBEAT: SOURCE_HARDWARE, EVT_LIGHT: SOURCE_ENVIRONMENT,
    EVT_SPEECH_STATE: SOURCE_AGENT,
}

# 按键 / 槽位 枚举
KEYS = ("yes", "no", "yes_always", "panel")
DOCK_SLOTS = ("portrait", "landscape", "none")


def now_ms() -> int:
    return int(time.time() * 1000)


class SeqGen:
    """单调递增序号生成器（每个连接一份）。"""

    def __init__(self) -> None:
        self._c = count(1)

    def next(self) -> int:
        return next(self._c)


def encode(seq_gen: SeqGen, t: str, **fields: Any) -> str:
    """构造一条协议消息为 JSON 字符串。"""
    msg = {"t": t, "seq": seq_gen.next(), "ts": now_ms()}
    msg.update(fields)
    return json.dumps(msg, ensure_ascii=False, separators=(",", ":"))


def decode(raw: str | bytes) -> dict[str, Any]:
    """解析一条协议消息。非法则返回 {'t': '_invalid', 'raw': ...}。"""
    if isinstance(raw, (bytes, bytearray)):
        raw = raw.decode("utf-8", "replace")
    try:
        obj = json.loads(raw)
        if not isinstance(obj, dict) or "t" not in obj:
            raise ValueError("missing 't'")
        return obj
    except Exception as exc:  # noqa: BLE001
        return {"t": "_invalid", "error": str(exc), "raw": raw}


# ---- 便捷构造器 (PC -> P4) ----

def m_speech_state(sg: SeqGen, state: str, utterance_id: str) -> str:
    return encode(sg, T_SPEECH_STATE, state=state, utterance_id=utterance_id)


def m_say(sg: SeqGen, text: str, utterance_id: str = "") -> str:
    """PC->P4：一句要朗读/显示的文本（导演压缩后的结果或助手输出）。"""
    return encode(sg, T_SAY, text=text, utterance_id=utterance_id)


def m_avatar(sg: SeqGen, action: str = "idle", expression: str = "neutral",
             emotion: str = "neutral", intensity: float = 0.5) -> str:
    return encode(sg, T_AVATAR, action=action, expression=expression,
                  emotion=emotion, intensity=intensity)


def m_work_status(sg: SeqGen, state: str, detail: str = "") -> str:
    # state: thinking|editing|running|blocked|done|error
    return encode(sg, T_WORK_STATUS, state=state, detail=detail)


def m_panel(sg: SeqGen, tokens: dict[str, int], cost_usd: float = 0.0,
            session: str = "", model: str = "", turns: int = 0) -> str:
    return encode(sg, T_PANEL, tokens=tokens, cost_usd=cost_usd,
                  session=session, model=model, turns=turns)


def m_prompt_confirm(sg: SeqGen, confirm_id: str, reason: str,
                     options: tuple[str, ...] = ("yes", "no", "yes_always")) -> str:
    return encode(sg, T_PROMPT_CONFIRM, id=confirm_id, reason=reason,
                  options=list(options))


# ---- 便捷构造器 (P4 -> PC) ----

def m_button(sg: SeqGen, key: str, event: str = "press",
             confirm_id: str | None = None) -> str:
    fields: dict[str, Any] = {"key": key, "event": event}
    if confirm_id is not None:
        fields["confirm_id"] = confirm_id
    return encode(sg, T_BUTTON, **fields)


def m_dock(sg: SeqGen, slot: str, present: bool) -> str:
    return encode(sg, T_DOCK, slot=slot, present=present)


def m_user_text(sg: SeqGen, text: str) -> str:
    """P4->PC：用户文本指令（ASR 结果；sim 用它模拟说话）。"""
    return encode(sg, T_USER_TEXT, text=text)


def m_vad(sg: SeqGen, state: str) -> str:
    # state: speech_start|speech_end|barge_in
    return encode(sg, T_VAD, state=state)


def m_hello(sg: SeqGen, fw: str, board: str, caps: list[str]) -> str:
    return encode(sg, T_HELLO, fw=fw, board=board, caps=caps)


def m_heartbeat(sg: SeqGen, rssi: int = 0, free_psram: int = 0) -> str:
    return encode(sg, T_HEARTBEAT, rssi=rssi, free_psram=free_psram)


# ═══════ Character Event Protocol (v1, ADR-006) ═══════

@dataclass
class CharacterEvent:
    source: str
    type: str
    data: dict[str, Any] = field(default_factory=dict)
    seq: int = 0
    ts: int = 0


def event_encode(sg: SeqGen, evt_type: str, data: dict[str, Any] | None = None,
                 source: str = "") -> str:
    """构造一条 CharacterEvent JSON。source 可通过 _EVT_SOURCE 自动推导。"""
    src = source or _EVT_SOURCE.get(evt_type, SOURCE_AGENT)
    msg: dict[str, Any] = {
        "source": src, "type": evt_type,
        "data": data or {},
        "seq": sg.next(), "ts": now_ms(),
    }
    return json.dumps(msg, ensure_ascii=False, separators=(",", ":"))


def decode_event(raw: str | bytes) -> CharacterEvent:
    """解析一条消息为 CharacterEvent。自动识别 v0 flat 和 v1 event 两种格式。"""
    obj = _parse_obj(raw)
    if "source" in obj:
        # v1 CharacterEvent
        return CharacterEvent(
            source=str(obj.get("source", "")),
            type=str(obj.get("type", "")),
            data=obj.get("data", {}) or {},
            seq=int(obj.get("seq", 0)),
            ts=int(obj.get("ts", 0)),
        )
    # v0 flat — 映射到 event
    t = obj.get("t", "")
    data = {k: v for k, v in obj.items() if k not in ("t", "seq", "ts")}
    evt_type = _flat_to_event_type(t)
    return CharacterEvent(
        source=_EVT_SOURCE.get(evt_type, SOURCE_AGENT),
        type=evt_type,
        data=data,
        seq=int(obj.get("seq", 0)),
        ts=int(obj.get("ts", 0)),
    )


def is_event(raw: str | bytes) -> bool:
    """判断一条消息是否为 v1 CharacterEvent 格式（含 'source' 字段）。"""
    obj = _parse_obj(raw)
    return "source" in obj


def _parse_obj(raw: str | bytes) -> dict[str, Any]:
    if isinstance(raw, (bytes, bytearray)):
        raw = raw.decode("utf-8", "replace")
    try:
        obj = json.loads(raw)
        if not isinstance(obj, dict):
            return {}
        return obj
    except Exception:  # noqa: BLE001
        return {}


def _flat_to_event_type(t: str) -> str:
    """v0 flat 't' → v1 event type。"""
    return {
        T_SAY: EVT_SPEECH, T_AVATAR: EVT_EMOTION, T_WORK_STATUS: EVT_STATUS,
        T_PANEL: EVT_PANEL, T_PROMPT_CONFIRM: EVT_PERMISSION,
        T_BUTTON: EVT_BUTTON, T_DOCK: EVT_PRESENCE, T_VAD: EVT_VAD,
        T_USER_TEXT: EVT_USER_INPUT, T_HELLO: EVT_HELLO, T_HEARTBEAT: EVT_HEARTBEAT,
        T_SPEECH_STATE: EVT_SPEECH_STATE,
    }.get(t, t)


# ---- CharacterEvent 便捷构造器 (替代或补充 m_*()) ----

def e_speech(sg: SeqGen, text: str, utterance_id: str = "") -> str:
    return event_encode(sg, EVT_SPEECH, {"text": text, "utterance_id": utterance_id})

def e_emotion(sg: SeqGen, action: str = "idle", emotion: str = "neutral",
              intensity: float = 0.5) -> str:
    return event_encode(sg, EVT_EMOTION,
                        {"action": action, "emotion": emotion, "intensity": intensity})

def e_status(sg: SeqGen, state: str, detail: str = "") -> str:
    return event_encode(sg, EVT_STATUS, {"state": state, "detail": detail})

def e_panel(sg: SeqGen, tokens: dict[str, int], cost_usd: float = 0.0,
            session: str = "", model: str = "", turns: int = 0) -> str:
    return event_encode(sg, EVT_PANEL,
                        {"tokens": tokens, "cost_usd": cost_usd,
                         "session": session, "model": model, "turns": turns})

def e_permission(sg: SeqGen, confirm_id: str, reason: str,
                 options: tuple[str, ...] = ("yes", "no", "yes_always")) -> str:
    return event_encode(sg, EVT_PERMISSION,
                        {"id": confirm_id, "reason": reason, "options": list(options)})

def e_button(sg: SeqGen, key: str, confirm_id: str | None = None) -> str:
    data: dict[str, Any] = {"key": key}
    if confirm_id: data["confirm_id"] = confirm_id
    return event_encode(sg, EVT_BUTTON, data)

def e_presence(sg: SeqGen, slot: str, present: bool) -> str:
    return event_encode(sg, EVT_PRESENCE, {"slot": slot, "present": present})

def e_user_input(sg: SeqGen, text: str) -> str:
    return event_encode(sg, EVT_USER_INPUT, {"text": text})

def e_hello(sg: SeqGen, fw: str, board: str, caps: list[str]) -> str:
    return event_encode(sg, EVT_HELLO, {"fw": fw, "board": board, "caps": caps})

def e_heartbeat(sg: SeqGen, rssi: int = 0, free_psram: int = 0) -> str:
    return event_encode(sg, EVT_HEARTBEAT, {"rssi": rssi, "free_psram": free_psram})

def e_tool_event(sg: SeqGen, evt: str, tool_name: str, tool_input: dict[str, Any] | None = None) -> str:
    """tool_start / tool_end 事件。"""
    return event_encode(sg, evt, {"tool": tool_name, "input": tool_input or {}})


@dataclass
class PendingConfirm:
    """pc-host 侧记录未决的工具授权请求，等待按键回填。"""

    confirm_id: str
    reason: str
    created_ms: int = field(default_factory=now_ms)
