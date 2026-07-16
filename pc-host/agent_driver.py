"""Agent 驱动抽象层。

把「被控制的 AI 代理」（Claude Code / 小龙虾 等）抽象成统一接口，host 只依赖本接口，
便于：① 用 FakeDriver 无成本、无真实工具执行地测试全链路；② 未来替换不同代理后端。

驱动通过回调把事件推给 host（host 再转成 DataChannel 消息）：
- on_status(state, detail)         代理工作状态: thinking|running|editing|done|error
- on_say(text)                     一句要朗读/显示的助手文本（导演压缩前的原文或分段）
- on_panel(usage)                  token 用量/成本 (Usage)
- on_permission(req)               需要用户授权的工具调用 (PermissionRequest)
- on_log(msg)                      调试信息（可选）

host 收到 on_permission 后，异步等用户按键，再调用 driver.resolve_permission(id, allow)。
"""
from __future__ import annotations

import abc
from dataclasses import dataclass, field
from typing import Any, Callable


@dataclass
class Usage:
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_input_tokens: int = 0
    cache_creation_input_tokens: int = 0
    cost_usd: float = 0.0
    session: str = ""
    model: str = ""
    turns: int = 0

    @property
    def total(self) -> int:
        return self.input_tokens + self.output_tokens

    def as_panel_tokens(self) -> dict[str, int]:
        return {
            "input": self.input_tokens,
            "output": self.output_tokens,
            "cache_read": self.cache_read_input_tokens,
            "total": self.total,
        }


@dataclass
class PermissionRequest:
    request_id: str
    tool_name: str
    tool_input: dict[str, Any] = field(default_factory=dict)
    risk_level: int = 1  # ADR-008: 0=LOW, 1=MEDIUM, 2=HIGH

    def human_reason(self) -> str:
        """给用户看的一句话（面板/语音提醒用），含风险等级前缀。"""
        prefix = {0: "[自动]", 1: "[确认]", 2: "[⚠️高风险]"}.get(self.risk_level, "")
        detail = ""
        if self.tool_name in ("Bash",):
            detail = self.tool_input.get("command", "")
        elif self.tool_name in ("Write", "Edit"):
            detail = self.tool_input.get("file_path", "")
        detail = (detail[:80] + "…") if len(detail) > 80 else detail
        return f"{prefix} {self.tool_name}" + (f": {detail}" if detail else " ?")


@dataclass
class DriverCallbacks:
    on_status: Callable[[str, str], None] = lambda s, d: None
    on_say: Callable[[str], None] = lambda t: None
    on_panel: Callable[[Usage], None] = lambda u: None
    on_permission: Callable[[PermissionRequest], None] = lambda r: None
    on_log: Callable[[str], None] = lambda m: None


class AgentDriver(abc.ABC):
    def __init__(self, cb: DriverCallbacks) -> None:
        self.cb = cb

    @abc.abstractmethod
    async def start(self) -> None: ...

    @abc.abstractmethod
    async def send_user(self, text: str) -> None:
        """把一句用户指令（ASR 文本）送给代理。"""

    @abc.abstractmethod
    async def resolve_permission(self, request_id: str, allow: bool,
                                 updated_input: dict[str, Any] | None = None,
                                 remember: bool = False) -> None:
        """回应一次工具授权。remember=True 对应 yes_always。"""

    @abc.abstractmethod
    async def stop(self) -> None: ...
