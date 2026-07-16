"""ClaudeSdkDriver - 用官方 Python Agent SDK (claude-agent-sdk) 驱动真实 Claude Code。

相比 claude_driver.py（原始 CLI 子进程），SDK 提供一等的 `can_use_tool` 权限回调，
天然映射到 4 按键（yes/no/yes_always）--这是真实**逐工具授权回路**的推荐实现。
SDK 内部仍拉起 `claude` CLI，故沿用用户 ccswitch 的后端路由（当前=DeepSeek V4 Pro）。

API 依 claude-agent-sdk v0.2.118 实测确认：
  ClaudeAgentOptions(can_use_tool, permission_mode, cwd, model, hooks, ...)
  ClaudeSDKClient.connect()/query(prompt)/receive_messages()/disconnect()
  can_use_tool: async (tool_name:str, input:dict, ctx:ToolPermissionContext) -> Allow|Deny
"""
from __future__ import annotations

import asyncio
from itertools import count
from typing import Any

from agent_driver import AgentDriver, DriverCallbacks, PermissionRequest, Usage
from risk import RiskLevel, classify  # ADR-008

try:
    import claude_agent_sdk as sdk
    _SDK_OK = True
except Exception:  # noqa: BLE001
    sdk = None  # type: ignore
    _SDK_OK = False


class ClaudeSdkDriver(AgentDriver):
    def __init__(self, cb: DriverCallbacks, cwd: str | None = None,
                 permission_mode: str = "default", model: str | None = None) -> None:
        super().__init__(cb)
        if not _SDK_OK:
            raise RuntimeError("未安装 claude-agent-sdk：pip install claude-agent-sdk")
        self.cwd = cwd
        self.permission_mode = permission_mode
        self.model = model
        self.client: Any = None
        self._pump: asyncio.Task | None = None
        self._pending: dict[str, asyncio.Future] = {}
        self._ids = count(1)
        self._usage = Usage()
        self._ready = asyncio.Event()   # connect() 完成前 send_user 需等待，避免竞态

    async def start(self) -> None:
        opts_kwargs: dict[str, Any] = dict(
            can_use_tool=self._can_use_tool,
            permission_mode=self.permission_mode,
            cwd=self.cwd,
        )
        if self.model:
            opts_kwargs["model"] = self.model
        options = sdk.ClaudeAgentOptions(**opts_kwargs)
        self.client = sdk.ClaudeSDKClient(options)
        await self.client.connect()
        self._pump = asyncio.ensure_future(self._pump_messages())
        self._ready.set()
        self.cb.on_log("ClaudeSdkDriver 已连接")

    async def send_user(self, text: str) -> None:
        await self._ready.wait()   # 等 connect() 完成
        self.cb.on_status("thinking", f"已提交：{text}")
        await self.client.query(text)

    # ---- 权限回调：SDK 调用此函数决定是否允许工具 ----
    async def _can_use_tool(self, tool_name: str, tool_input: dict[str, Any], context: Any):
        # ADR-008: Risk Classifier - LOW 自动放行
        level, reason = classify(tool_name, tool_input or {})
        if level == RiskLevel.LOW:
            self.cb.on_log(f"风险: LOW {tool_name} - 自动放行 ({reason})")
            self.cb.on_status("running", f"自动执行 {tool_name}")
            return sdk.PermissionResultAllow(updated_input=tool_input or {})

        rid = getattr(context, "tool_use_id", None) or f"perm_{next(self._ids)}"
        req = PermissionRequest(request_id=rid, tool_name=tool_name,
                                tool_input=tool_input or {}, risk_level=int(level))
        loop = asyncio.get_event_loop()
        fut: asyncio.Future = loop.create_future()
        self._pending[rid] = fut
        self.cb.on_status("blocked", req.human_reason())
        self.cb.on_permission(req)
        try:
            allow, remember, upd = await asyncio.wait_for(fut, timeout=120)
        except asyncio.TimeoutError:
            return sdk.PermissionResultDeny(message="授权超时")
        finally:
            self._pending.pop(rid, None)

        if not allow:
            return sdk.PermissionResultDeny(message="用户按下拒绝键")
        kwargs: dict[str, Any] = {"updated_input": upd if upd is not None else tool_input}
        if remember:
            try:
                kwargs["updated_permissions"] = [{
                    "type": "addRules",
                    "rules": [{"toolName": tool_name, "ruleContent": "*"}],
                    "behavior": "allow",
                    "destination": "session",
                }]
            except Exception:  # noqa: BLE001
                self.cb.on_log("yes_always 规则构造失败，退化为单次放行")
        return sdk.PermissionResultAllow(**kwargs)

    async def resolve_permission(self, request_id: str, allow: bool,
                                 updated_input: dict[str, Any] | None = None,
                                 remember: bool = False) -> None:
        fut = self._pending.get(request_id)
        if fut and not fut.done():
            fut.set_result((allow, remember, updated_input))
        self.cb.on_log(f"resolve {request_id} allow={allow} remember={remember}")

    # ---- 持续消费 SDK 消息 ----
    async def _pump_messages(self) -> None:
        try:
            async for msg in self.client.receive_messages():
                self._dispatch(msg)
        except asyncio.CancelledError:
            pass
        except Exception as exc:  # noqa: BLE001
            self.cb.on_log(f"消息泵异常: {exc}")
            self.cb.on_status("error", str(exc))

    def _dispatch(self, msg: Any) -> None:
        name = type(msg).__name__
        if name == "AssistantMessage":
            self._merge_usage(getattr(msg, "usage", None))
            for block in getattr(msg, "content", []) or []:
                bn = type(block).__name__
                if bn == "TextBlock":
                    text = (getattr(block, "text", "") or "").strip()
                    if text:
                        self.cb.on_say(text)
                elif bn == "ToolUseBlock":
                    self.cb.on_status("editing", f"调用工具 {getattr(block, 'name', '?')}")
        elif name == "UserMessage":
            self.cb.on_status("running", "工具执行中…")
        elif name == "ResultMessage":
            self._merge_usage(getattr(msg, "usage", None))
            self._usage.cost_usd = float(getattr(msg, "total_cost_usd", None) or self._usage.cost_usd)
            self._usage.turns = int(getattr(msg, "num_turns", None) or self._usage.turns)
            self._usage.session = getattr(msg, "session_id", None) or self._usage.session
            self.cb.on_status("error" if getattr(msg, "is_error", False) else "done",
                              getattr(msg, "subtype", "") or "")
            self.cb.on_panel(self._usage)
        elif name == "SystemMessage":
            data = getattr(msg, "data", {}) or {}
            if getattr(msg, "subtype", "") == "init":
                self._usage.session = data.get("session_id", self._usage.session)
                self._usage.model = data.get("model", self._usage.model)
                self.cb.on_log(f"init session={self._usage.session} model={self._usage.model}")
        # StreamEvent / RateLimitEvent 暂忽略

    def _merge_usage(self, u: Any) -> None:
        if not isinstance(u, dict):
            return
        self._usage.input_tokens = int(u.get("input_tokens", self._usage.input_tokens))
        self._usage.output_tokens = int(u.get("output_tokens", self._usage.output_tokens))
        self._usage.cache_read_input_tokens = int(
            u.get("cache_read_input_tokens", self._usage.cache_read_input_tokens))
        self._usage.cache_creation_input_tokens = int(
            u.get("cache_creation_input_tokens", self._usage.cache_creation_input_tokens))

    async def stop(self) -> None:
        if self._pump:
            self._pump.cancel()
        for fut in self._pending.values():
            if not fut.done():
                fut.cancel()
        self._pending.clear()
        if self.client:
            try:
                await self.client.disconnect()
            except Exception:  # noqa: BLE001
                pass
