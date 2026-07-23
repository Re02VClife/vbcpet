"""serial_bridge.py — Claude Code ↔ USB Serial ↔ P4 firmware bridge (v0.1)

v0.1: 文字交互闭环 — Claude 说的话显示到 P4 屏幕，PC 键盘发送用户文字。
v0.2: + Live2D JPEG 推流 + 按键模拟 (/y /n /a) + permission 回路。

用法:
  .venv/Scripts/python pc-host/serial_bridge.py [--port COM16] [--baud 2000000]
"""

from __future__ import annotations

import asyncio
import json
import sys
import time
from itertools import count
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_driver import DriverCallbacks, Usage, PermissionRequest  # noqa: E402
from claude_sdk_driver import ClaudeSdkDriver  # noqa: E402


class SerialBridge:
    def __init__(self, port: str = "COM16", baud: int = 2_000_000):
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=3)
        self._seq = count(1)
        self._driver: ClaudeSdkDriver | None = None
        self._pending_perm: PermissionRequest | None = None  # 最近一条待授权请求

    # ── PC → P4: CTRL 消息发送 ──

    def _send_ctrl(self, obj: dict) -> None:
        """发送 CTRL:<json>\\n 到 P4"""
        obj["seq"] = next(self._seq)
        obj["ts"] = int(time.time() * 1000)
        payload = json.dumps(obj, ensure_ascii=False)
        line = f"CTRL:{payload}\n"
        self.ser.write(line.encode())
        self.ser.flush()

    # ── AgentDriver 回调 → CTRL 消息 ──

    def on_say(self, text: str) -> None:
        print(f"\n[Claude] {text[:120]}{'…' if len(text) > 120 else ''}")
        self._send_ctrl({"t": "say", "text": text})

    def on_status(self, state: str, detail: str) -> None:
        print(f"[status] {state}: {detail}")
        self._send_ctrl({"t": "work_status", "state": state, "detail": detail})

    def on_panel(self, usage: Usage) -> None:
        print(f"[panel] {usage.total} tokens, ${usage.cost_usd:.4f}")
        self._send_ctrl({
            "t": "panel",
            "tokens": {
                "input": usage.input_tokens,
                "output": usage.output_tokens,
                "cache_read": usage.cache_read_input_tokens,
                "total": usage.total,
            },
            "cost_usd": usage.cost_usd,
        })

    def on_permission(self, req: PermissionRequest) -> None:
        self._pending_perm = req
        print(f"\n[perm] {req.human_reason()}")
        print(f"       输入 /y=允许  /n=拒绝  /a=允许+记住")
        self._send_ctrl({
            "t": "prompt_confirm",
            "id": req.request_id,
            "reason": req.human_reason(),
        })

    # ── P4 → PC: 串口读取 ──

    async def _read_serial(self) -> None:
        loop = asyncio.get_event_loop()
        buf = b""
        while True:
            # 非阻塞读可用字节
            chunk = await loop.run_in_executor(None, self.ser.read, max(1, self.ser.in_waiting or 1))
            if not chunk:
                await asyncio.sleep(0.01)
                continue
            buf += chunk
            while b"\n" in buf:
                line_bytes, buf = buf.split(b"\n", 1)
                line = line_bytes.decode("utf-8", errors="replace").strip()
                self._handle_serial_line(line)

    def _handle_serial_line(self, line: str) -> None:
        if not line.startswith("CTRL:"):
            return
        try:
            msg = json.loads(line[5:])
        except json.JSONDecodeError:
            return
        t = msg.get("t", "")
        if t == "hello":
            print(f"\n[P4] 已连接 — fw={msg.get('fw')} board={msg.get('board')} caps={msg.get('caps')}")
        elif t == "heartbeat":
            pass  # 静默，太吵
        elif t == "button":
            key = msg.get("key", "")
            cid = msg.get("confirm_id", "")
            print(f"[P4] 按键: {key}")
        else:
            print(f"[P4] {line[:100]}")

    # ── PC 键盘 → Claude ──

    async def _read_stdin(self) -> None:
        loop = asyncio.get_event_loop()
        print("\n输入文字发送给 Claude，/y /n /a 回应授权，/q 退出\n")
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            line = line.strip()
            if not line:
                continue
            if line == "/q":
                print("退出…")
                if self._driver:
                    await self._driver.stop()
                sys.exit(0)
            elif line in ("/y", "/yes"):
                await self._resolve_current(True, False)
            elif line in ("/n", "/no"):
                await self._resolve_current(False, False)
            elif line in ("/a", "/yes_always"):
                await self._resolve_current(True, True)
            elif line == "/p":
                print("[panel] 面板切换 (v0.2)")
            else:
                print(f"[user] 发送: {line}")
                if self._driver:
                    await self._driver.send_user(line)

    async def _resolve_current(self, allow: bool, remember: bool) -> None:
        if not self._pending_perm:
            print("   (无待授权请求)")
            return
        if not self._driver:
            return
        req = self._pending_perm
        self._pending_perm = None
        action = "允许+记住" if remember else ("允许" if allow else "拒绝")
        print(f"   → {action}: {req.tool_name}")
        await self._driver.resolve_permission(req.request_id, allow, remember=remember)

    # ── 主循环 ──

    async def run(self) -> None:
        cb = DriverCallbacks(
            on_say=self.on_say,
            on_status=self.on_status,
            on_panel=self.on_panel,
            on_permission=self.on_permission,
            on_log=lambda m: print(f"[claude] {m}"),
        )
        self._driver = ClaudeSdkDriver(cb)
        await self._driver.start()
        print(f"Claude SDK 已连接，串口: {self.ser.port} @ {self.ser.baudrate} baud")

        await asyncio.gather(
            self._read_serial(),
            self._read_stdin(),
        )


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="vbcpet Claude ↔ P4 Serial Bridge")
    ap.add_argument("--port", default="COM16")
    ap.add_argument("--baud", type=int, default=2_000_000)
    args = ap.parse_args()
    bridge = SerialBridge(port=args.port, baud=args.baud)
    asyncio.run(bridge.run())
