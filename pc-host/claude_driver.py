"""ClaudeDriver — 用长驻 `claude` 子进程驱动真实 Claude Code（headless 双向 stream-json）。

输出事件 schema 已用本机 claude v2.1.185 实测确认：
  system/init · system/thinking_tokens · assistant(message.content=[thinking|text|tool_use])
  · user(tool_result) · result(usage/total_cost_usd/session_id/num_turns)

工具授权（control_request/control_response）的确切形状随版本变化，本实现按当前理解编写，
以 claude-code-guide 的调研结论为准校正（见 pc-host/README.md 的“授权协议”小节）。
"""
from __future__ import annotations

import asyncio
import json
import shutil
import sys
from typing import Any

from agent_driver import AgentDriver, DriverCallbacks, PermissionRequest, Usage


def _resolve_cmd(cmd: str) -> list[str]:
    """把 claude 命令解析成 create_subprocess_exec 可用的 argv 前缀（Windows 兼容 .cmd）。"""
    path = shutil.which(cmd) or cmd
    if sys.platform == "win32" and path.lower().endswith((".cmd", ".bat")):
        # .cmd 需经 cmd.exe 执行
        return ["cmd", "/c", path]
    return [path]


class ClaudeDriver(AgentDriver):
    def __init__(self, cb: DriverCallbacks, cmd: str = "claude",
                 cwd: str | None = None, extra_args: list[str] | None = None) -> None:
        super().__init__(cb)
        self.cmd = cmd
        self.cwd = cwd
        self.extra_args = extra_args or []
        self.proc: asyncio.subprocess.Process | None = None
        self._usage = Usage()
        self._reader: asyncio.Task | None = None
        self._stderr_reader: asyncio.Task | None = None
        self._pending_inputs: dict[str, dict[str, Any]] = {}  # request_id -> 原始 tool input

    async def start(self) -> None:
        argv = _resolve_cmd(self.cmd) + [
            "-p",
            "--output-format", "stream-json",
            "--input-format", "stream-json",
            "--verbose",
            "--permission-mode", "default",
            *self.extra_args,
        ]
        self.cb.on_log(f"启动: {' '.join(argv)}")
        self.proc = await asyncio.create_subprocess_exec(
            *argv, cwd=self.cwd,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        self._reader = asyncio.ensure_future(self._read_stdout())
        self._stderr_reader = asyncio.ensure_future(self._read_stderr())

    async def _write(self, obj: dict[str, Any]) -> None:
        if not self.proc or not self.proc.stdin:
            return
        self.proc.stdin.write((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
        await self.proc.stdin.drain()

    async def send_user(self, text: str) -> None:
        await self._write({
            "type": "user",
            "message": {"role": "user", "content": [{"type": "text", "text": text}]},
        })
        self.cb.on_status("thinking", f"已提交：{text}")

    async def resolve_permission(self, request_id: str, allow: bool,
                                 updated_input: dict[str, Any] | None = None,
                                 remember: bool = False) -> None:
        # control_response 形状据 claude-code-guide 调研：allow 必须回 updatedInput（镜像原 input）
        orig = self._pending_inputs.pop(request_id, {})
        behavior = "allow" if allow else "deny"
        inner: dict[str, Any] = {"behavior": behavior}
        if allow:
            inner["updatedInput"] = updated_input if updated_input is not None else orig
        else:
            inner["message"] = "用户按下拒绝键"
        await self._write({
            "type": "control_response",
            "response": {"subtype": "success", "request_id": request_id, "response": inner},
        })
        self.cb.on_log(f"control_response {request_id} behavior={behavior} remember={remember}")

    async def _read_stdout(self) -> None:
        assert self.proc and self.proc.stdout
        while True:
            line = await self.proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                self.cb.on_log(f"非 JSON 行: {line[:120]!r}")
                continue
            self._handle_event(ev)
        self.cb.on_status("done", "claude 进程结束")

    async def _read_stderr(self) -> None:
        assert self.proc and self.proc.stderr
        while True:
            line = await self.proc.stderr.readline()
            if not line:
                break
            self.cb.on_log("stderr: " + line.decode("utf-8", "replace").rstrip())

    def _handle_event(self, ev: dict[str, Any]) -> None:
        t = ev.get("type")
        if t == "system":
            sub = ev.get("subtype")
            if sub == "init":
                self._usage.session = ev.get("session_id", "")
                self._usage.model = ev.get("model", "")
                self.cb.on_log(f"init session={self._usage.session} model={self._usage.model}")
            # thinking_tokens 忽略（仅用于精细进度，可选接入）
        elif t == "assistant":
            self._on_assistant(ev.get("message", {}))
        elif t == "user":
            # 工具结果回显，可用于状态提示
            self.cb.on_status("running", "工具执行中…")
        elif t == "result":
            self._on_result(ev)
        elif t == "control_request":
            self._on_control_request(ev)
        else:
            self.cb.on_log(f"未知事件: {t}")

    def _on_assistant(self, message: dict[str, Any]) -> None:
        usage = message.get("usage") or {}
        if usage:
            self._merge_usage(usage)
        for block in message.get("content", []):
            bt = block.get("type")
            if bt == "text":
                text = block.get("text", "").strip()
                if text:
                    self.cb.on_say(text)          # 原文；导演压缩层后续接在此之前
                    self.cb.on_status("thinking", "")
            elif bt == "tool_use":
                name = block.get("name", "?")
                self.cb.on_status("editing", f"调用工具 {name}")

    def _on_result(self, ev: dict[str, Any]) -> None:
        self._merge_usage(ev.get("usage") or {})
        self._usage.cost_usd = float(ev.get("total_cost_usd") or self._usage.cost_usd)
        self._usage.turns = int(ev.get("num_turns") or self._usage.turns)
        self._usage.session = ev.get("session_id", self._usage.session)
        err = ev.get("is_error")
        self.cb.on_status("error" if err else "done", ev.get("subtype", ""))
        self.cb.on_panel(self._usage)

    def _on_control_request(self, ev: dict[str, Any]) -> None:
        req = ev.get("request", {})
        if req.get("subtype") == "can_use_tool":
            rid = ev.get("request_id", "")
            tool_input = req.get("input", {}) or {}
            self._pending_inputs[rid] = tool_input   # 供 allow 时镜像回 updatedInput
            pr = PermissionRequest(request_id=rid, tool_name=req.get("tool_name", "?"),
                                   tool_input=tool_input)
            self.cb.on_status("blocked", pr.human_reason())
            self.cb.on_permission(pr)
        else:
            self.cb.on_log(f"其他 control_request: {req.get('subtype')}")

    def _merge_usage(self, u: dict[str, Any]) -> None:
        self._usage.input_tokens = int(u.get("input_tokens", self._usage.input_tokens))
        self._usage.output_tokens = int(u.get("output_tokens", self._usage.output_tokens))
        self._usage.cache_read_input_tokens = int(
            u.get("cache_read_input_tokens", self._usage.cache_read_input_tokens))
        self._usage.cache_creation_input_tokens = int(
            u.get("cache_creation_input_tokens", self._usage.cache_creation_input_tokens))

    async def stop(self) -> None:
        for task in (self._reader, self._stderr_reader):
            if task:
                task.cancel()
        if self.proc and self.proc.returncode is None:
            try:
                if self.proc.stdin:
                    self.proc.stdin.close()
                self.proc.terminate()
            except ProcessLookupError:
                pass
