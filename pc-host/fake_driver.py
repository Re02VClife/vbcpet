"""FakeDriver — 脚本化的假代理，模拟一次「代理式编程任务」的事件流。

无成本、不执行任何真实工具，用于在没有 live claude / 没有硬件时端到端验证：
用户文本 → 状态/说话 → 请求工具授权 → 等按键 → 放行/拒绝 → 结果 + token 面板。
"""
from __future__ import annotations

import asyncio
from itertools import count
from typing import Any

from agent_driver import AgentDriver, DriverCallbacks, PermissionRequest, Usage


class FakeDriver(AgentDriver):
    def __init__(self, cb: DriverCallbacks) -> None:
        super().__init__(cb)
        self._ids = count(1)
        self._pending: dict[str, asyncio.Future] = {}
        self._usage = Usage(session="fake-sess", model="fake-agent")

    async def start(self) -> None:
        self.cb.on_log("FakeDriver 就绪")

    async def send_user(self, text: str) -> None:
        asyncio.ensure_future(self._run_task(text))

    async def _run_task(self, text: str) -> None:
        self._usage.turns += 1
        self._bump(1200, 60)
        self.cb.on_status("thinking", f"理解指令：{text}")
        self.cb.on_say(f"收到用户指令：{text}。正在解析需求并分析代码结构，评估多个技术方案的可行性和性能影响。目前已经加载了项目相关的全部上下文信息，包括 27 个源文件的结构索引和最近 3 次 git 提交的变更记录。正在逐一检查 npm 依赖树的版本兼容性——共 312 个包中有 5 个需要升级。接下来将启动自动化测试流程。")
        await asyncio.sleep(1.0)

        self._bump(300, 90)
        self.cb.on_status("editing", "准备运行测试")
        # 请求工具授权
        req = PermissionRequest(request_id=f"fake_{next(self._ids)}",
                                tool_name="Bash", tool_input={"command": "npm test"})
        fut: asyncio.Future = asyncio.get_event_loop().create_future()
        self._pending[req.request_id] = fut
        self.cb.on_status("blocked", "等待授权：npm test")
        self.cb.on_permission(req)

        try:
            allow = await asyncio.wait_for(fut, timeout=60)
        except asyncio.TimeoutError:
            allow = False
            self.cb.on_log("授权超时，按拒绝处理")

        if allow:
            self._bump(500, 400)
            self.cb.on_status("running", "正在运行 npm test…")
            await asyncio.sleep(1.2)
            self._bump(200, 350)
            self.cb.on_status("done", "测试通过")
            self.cb.on_say("自动化测试流程已经执行完毕。所有 42 个单元测试全部通过，0 个失败，代码覆盖率 94%。静态检查无告警。总结：本次改动安全，可以提交。")
        else:
            self.cb.on_status("done", "已取消")
            self.cb.on_say("好的，那我先不运行测试。")
        self.cb.on_panel(self._usage)

    def _bump(self, din: int, dout: int) -> None:
        self._usage.input_tokens += din
        self._usage.output_tokens += dout
        self._usage.cost_usd = round(self._usage.total * 3e-6, 4)
        self.cb.on_panel(self._usage)

    async def resolve_permission(self, request_id: str, allow: bool,
                                 updated_input: dict[str, Any] | None = None,
                                 remember: bool = False) -> None:
        fut = self._pending.pop(request_id, None)
        if fut and not fut.done():
            fut.set_result(allow)
        self.cb.on_log(f"授权结果 {request_id}: allow={allow} remember={remember}")

    async def stop(self) -> None:
        for fut in self._pending.values():
            if not fut.done():
                fut.cancel()
        self._pending.clear()
