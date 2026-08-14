"""ChatDriver — 轻量聊天驱动 (直连 Anthropic 兼容端点, 无 Claude Code CLI 系统提示)

与 ClaudeSdkDriver (dev 模式) 对比:
  - dev 模式: 拉起 claude CLI, 带完整系统提示+工具 → 每次固定 ~22k tokens 开销
  - chat 模式: 直接调 Messages API, 无工具, 系统提示仅几行人设 → token/成本降 ~90%

API key/base_url/model 从 ~/.claude/settings.json 的 env 读取 (与 Claude Code 同源,
即 ccswitch 路由的 DeepSeek)。同一后端模型, 但按"桌宠生活聊天"场景配置人设。
"""
from __future__ import annotations

import asyncio
import json
import os
from itertools import count
from typing import Any

import httpx

from agent_driver import AgentDriver, DriverCallbacks, Usage

CHAT_SYSTEM_PROMPT = """你是 vbcpet 桌面宠物里的 AI 小伙伴。用户通过桌面宠物和你聊天。
回答要求:
- 简短、口语化、亲切,像朋友聊天
- 默认用中文,除非用户用其他语言
- 不要用 markdown 语法、标题、列表、代码块
- 每条回复不超过 80 字
- 可以闲聊、安慰、逗趣,适配生活场景
- 不要提及工具、代码执行能力
"""

MAX_HISTORY = 10   # 保留最近对话轮数 (防 context 无限增长)


def _load_claude_env() -> dict[str, str]:
    """从 ~/.claude/settings.json 读 env (与 Claude Code 同源)"""
    try:
        p = os.path.expanduser("~/.claude/settings.json")
        return dict(json.load(open(p, encoding="utf-8")).get("env", {}))
    except Exception:
        return {}


class ChatDriver(AgentDriver):
    def __init__(self, cb: DriverCallbacks, model: str | None = None,
                 system_prompt: str | None = None) -> None:
        super().__init__(cb)
        env = _load_claude_env()
        self.base_url = (env.get("ANTHROPIC_BASE_URL") or
                         "https://api.deepseek.com/anthropic").rstrip("/")
        self.model = model or env.get("ANTHROPIC_MODEL") or "deepseek-v4-flash[1M]"
        self.auth_token = (env.get("ANTHROPIC_AUTH_TOKEN") or
                           os.environ.get("ANTHROPIC_AUTH_TOKEN") or "")
        self.system_prompt = system_prompt or CHAT_SYSTEM_PROMPT
        self._history: list[dict[str, str]] = []
        self._usage = Usage()
        self._ids = count(1)
        self._ready = asyncio.Event()

    async def start(self) -> None:
        if not self.auth_token:
            self.cb.on_log("[chat] 未找到 API key (settings.json env.ANTHROPIC_AUTH_TOKEN)")
        self._ready.set()

    async def send_user(self, text: str) -> None:
        await self._ready.wait()
        self.cb.on_status("thinking", f"已提交：{text}")
        messages = [*self._history, {"role": "user", "content": text}]
        body = {
            "model": self.model,
            "system": self.system_prompt,
            "messages": messages,
            "max_tokens": 1024,
        }
        headers = {"x-api-key": self.auth_token, "anthropic-version": "2023-06-01"}
        try:
            async with httpx.AsyncClient(timeout=120) as client:
                resp = await client.post(f"{self.base_url}/v1/messages",
                                         json=body, headers=headers)
                resp.raise_for_status()
                data = resp.json()
            reply = "".join(b.get("text", "") for b in data.get("content", [])
                            if isinstance(b, dict)).strip() or "(没有回复)"
            self._merge_usage(data.get("usage", {}))
            self._history.append({"role": "user", "content": text})
            self._history.append({"role": "assistant", "content": reply})
            if len(self._history) > MAX_HISTORY * 2:
                self._history = self._history[-MAX_HISTORY * 2:]
            self.cb.on_say(reply)
            self.cb.on_status("done", "chat")
            self.cb.on_panel(self._usage)
        except httpx.HTTPStatusError as e:
            detail = e.response.text[:200] if e.response is not None else ""
            self.cb.on_log(f"[chat] HTTP {e.response.status_code}: {detail}")
            self.cb.on_status("error", f"chat API {e.response.status_code}")
        except Exception as e:  # noqa: BLE001
            self.cb.on_log(f"[chat] 请求失败: {e}")
            self.cb.on_status("error", str(e))

    def _merge_usage(self, u: dict) -> None:
        self._usage.input_tokens = int(u.get("input_tokens", self._usage.input_tokens))
        self._usage.output_tokens = int(u.get("output_tokens", self._usage.output_tokens))

    async def resolve_permission(self, request_id: str, allow: bool,
                                 updated_input: dict[str, Any] | None = None,
                                 remember: bool = False) -> None:
        pass  # 纯聊天无工具

    async def stop(self) -> None:
        pass
