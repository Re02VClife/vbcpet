"""serial_bridge.py v0.2 — Live2D 推流 + Claude 交互 + 表情控制 + 按键模拟

单进程独占 COM16:
  - Live2D JPEG 推流 (Playwright 截图 → FRAME 协议)
  - Claude SDK 交互桥 (say/panel/work_status/prompt_confirm → CTRL 消息)
  - Claude 状态 → Live2D 表情/动作 (page.evaluate → playMotion/setExpression)
  - PC 键盘输入用户文字 + 按键模拟

用法:
  .venv/Scripts/python pc-host/serial_bridge.py [--port COM16] [--quality 80]
"""

from __future__ import annotations

import asyncio
import json
import sys
import time
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler
from itertools import count
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_driver import DriverCallbacks, Usage, PermissionRequest  # noqa: E402
from claude_sdk_driver import ClaudeSdkDriver  # noqa: E402


# ── HTTP 服务 (Live2D 静态文件) ──

class _Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "application/javascript", ".wasm": "application/wasm"}
    def log_message(self, f, *a): pass
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

def _start_http(root: str) -> tuple[HTTPServer, int]:
    import os, socket
    os.chdir(root)
    with socket.socket() as s: s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    httpd = HTTPServer(("127.0.0.1", port), _Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, port


class UnifiedBridge:
    def __init__(self, port: str = "COM16", baud: int = 2_000_000, jpeg_quality: int = 80):
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=3)
        self.jpeg_quality = jpeg_quality
        self._seq = count(1)
        self._driver: ClaudeSdkDriver | None = None
        self._pending_perm: PermissionRequest | None = None
        self._browser = None
        self._page = None
        self._model_motions: list[str] = []
        self._model_expressions: list[str] = []
        self._model_emotions: list[str] = []
        self._frame_count = 0
        self._fps_t0 = time.time()
        self._ctrl_queue: asyncio.Queue = None  # 初始化在 run() 中

    # ── PC → P4: CTRL 消息入队 (非阻塞) ──

    def _send_ctrl(self, obj: dict) -> None:
        """将 CTRL 消息入队, 由 _io_loop 串行发送 (避免与帧数据交替写)"""
        obj["seq"] = next(self._seq)
        obj["ts"] = int(time.time() * 1000)
        payload = json.dumps(obj, ensure_ascii=False)
        self._ctrl_queue.put_nowait(f"CTRL:{payload}\n".encode())

    # ── Live2D 控制 ──

    async def _l2d_motion(self, name: str) -> bool:
        if not self._page: return False
        try:
            return await self._page.evaluate(f'() => window.playMotion("{name}")')
        except Exception:
            return False

    async def _l2d_emotion(self, emotion: str, duration: int = 400) -> bool:
        """调用 render.html 的 applyEmotion — 带缓入缓出的参数过渡"""
        if not self._page: return False
        try:
            return await self._page.evaluate(
                f'() => window.applyEmotion("{emotion}", {duration})')
        except Exception:
            return False

    async def _apply_l2d_status(self, state: str) -> None:
        """Claude 状态 → Live2D 动作 + 表情"""
        emo_map = {
            "thinking": ("Idle", "neutral"),
            "running":  ("Idle", "neutral"),
            "editing":  ("Idle", "sad"),      # worried → sad (Hiyori 无 worried 预设)
            "done":     ("Idle", "happy"),
            "error":    ("Idle", "sad"),
            "blocked":  ("Idle", "surprised"),
        }
        motion, emotion = emo_map.get(state, ("Idle", "neutral"))
        await self._l2d_motion(motion)
        await self._l2d_emotion(emotion)

    # ── AgentDriver 回调 ──

    def on_say(self, text: str) -> None:
        print(f"\n[Claude] {text[:120]}{'…' if len(text) > 120 else ''}")
        # 截断到 ~300 字符 (P4 行缓冲 1024 字节, CJK ~3 字节/字)
        short = text if len(text) <= 300 else text[:297] + "..."
        self._send_ctrl({"t": "say", "text": short})

    def on_status(self, state: str, detail: str) -> None:
        print(f"[status] {state}: {detail}")
        # 不发送 work_status 到 P4 (减少 CTRL 消息频率)
        asyncio.ensure_future(self._apply_l2d_status(state))

    def on_panel(self, usage: Usage) -> None:
        print(f"[panel] {usage.total} tokens, ${usage.cost_usd:.4f}")
        # 面板仅记录日志, 不发送到 P4 (减少干扰)

    def on_permission(self, req: PermissionRequest) -> None:
        self._pending_perm = req
        print(f"\n[perm] {req.human_reason()}")
        print(f"       [1]允许  [2]拒绝  [3]允许+记住")
        self._send_ctrl({
            "t": "prompt_confirm",
            "id": req.request_id,
            "reason": req.human_reason(),
        })

    # ── 串口 I/O: 推流 + 收 P4 消息 (单线程避免并发锁冲突) ──

    def _handle_p4_line(self, line: str) -> None:
        if not line.startswith("CTRL:"):
            return
        try:
            msg = json.loads(line[5:])
        except json.JSONDecodeError:
            return
        t = msg.get("t", "")
        if t == "hello":
            print(f"[P4] 已连接 fw={msg.get('fw')} board={msg.get('board')} caps={msg.get('caps')}")
        elif t == "button":
            key = msg.get("key", "")
            cid = msg.get("confirm_id", "")
            print(f"[P4] 按键: {key}")
        elif t == "heartbeat":
            pass

    async def _io_loop(self) -> None:
        """单任务: 截图 → 推帧 → 读 P4 回复 → 重复"""
        print(f"Live2D streaming started (quality={self.jpeg_quality})")
        buf = b""
        loop = asyncio.get_event_loop()
        last_frame = 0.0
        min_interval = 1.0 / 15.0  # 上限 15fps, 防止 P4 串口溢出
        while True:
            # 帧率限制
            elapsed = time.time() - last_frame
            if elapsed < min_interval:
                await asyncio.sleep(min_interval - elapsed)
            last_frame = time.time()

            # 1. 截图
            try:
                jpeg = await asyncio.wait_for(
                    self._page.screenshot(type='jpeg', quality=self.jpeg_quality),
                    timeout=5.0)
            except asyncio.TimeoutError:
                await asyncio.sleep(0.5); continue
            except asyncio.CancelledError:
                return
            except Exception as e:
                print(f"[stream] screenshot error: {e}")
                await asyncio.sleep(1.0); continue

            # 2. 先发队列中的 CTRL 消息
            ctrl_sent = 0
            while not self._ctrl_queue.empty():
                try:
                    data = self._ctrl_queue.get_nowait()
                    self.ser.write(data)
                    # 诊断: 检查消息中的换行符
                    n_count = data.count(b'\n')
                    if n_count > 1:
                        print(f"  [ctrl-WARN] {len(data)}B, {n_count} newlines in one msg!", flush=True)
                    ctrl_sent += 1
                except Exception:
                    pass
            if ctrl_sent:
                self.ser.flush()
                print(f"  [ctrl] sent {ctrl_sent} msgs", flush=True)
                await asyncio.sleep(0.1)  # 100ms P4 处理 CTRL 行

            # 3. 写帧到串口
            if not self.ser.is_open:
                print("[stream] serial port closed, stopping")
                return
            header = f"FRAME:{len(jpeg):08x}\n".encode()
            try:
                self.ser.write(header)
                self.ser.write(jpeg)
                self.ser.flush()
            except Exception as e:
                print(f"[stream] write error: {e}")
                if not self.ser.is_open:
                    print("[stream] serial port lost, stopping")
                    return

            self._frame_count += 1
            if self._frame_count % 30 == 0:
                fps = self._frame_count / (time.time() - self._fps_t0)
                print(f"  [stream] {self._frame_count} frames, {fps:.1f} fps, {len(jpeg)//1024}KB/frame", flush=True)

            # 3. 排空 P4 回复 (防 USB TX 缓冲满 → 设备冻结)
            try:
                # 循环读取直到没有数据为止
                for _ in range(10):  # 最多 10 轮, 防止死循环
                    chunk = await loop.run_in_executor(None, self.ser.read, max(1, self.ser.in_waiting or 0))
                    if not chunk: break
                    buf += chunk
                while b"\n" in buf:
                    line_bytes, buf = buf.split(b"\n", 1)
                    self._handle_p4_line(line_bytes.decode("utf-8", errors="replace").strip())
            except Exception:
                pass

    # ── PC 键盘 → Claude ──

    async def _read_stdin(self) -> None:
        loop = asyncio.get_event_loop()
        print("\n┌─────────────────────────────────────────────┐")
        print("│ 输入文字 → 发给 Claude                       │")
        print("│ [1]=允许  [2]=拒绝  [3]=允许+记住  [4]=面板   │")
        print("│ /q=退出                                      │")
        print("└─────────────────────────────────────────────┘\n")

        while True:
            try:
                line = await loop.run_in_executor(None, sys.stdin.readline)
            except asyncio.CancelledError:
                return
            if not line:  # EOF
                print("[stdin] EOF, exiting...")
                break
            line = line.strip()
            if not line:
                continue
            if line == "/q":
                print("退出…")
                if self._driver:
                    await self._driver.stop()
                sys.exit(0)
            elif line == "1":
                await self._resolve_current(True, False)
            elif line == "2":
                await self._resolve_current(False, False)
            elif line == "3":
                await self._resolve_current(True, True)
            elif line == "4":
                print("[panel] 面板 (v0.2 占位) — 发送 panel 请求到 P4")
                self._send_ctrl({"t": "panel", "tokens": {"input": 0, "output": 0, "cache_read": 0, "total": 0}, "cost_usd": 0.0})
            else:
                print(f"[user] {line}")
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

    # ── 启动 ──

    async def _init_browser(self) -> None:
        repo = Path(__file__).resolve().parent.parent
        httpd, port = _start_http(str(repo / "live2d"))
        print(f"Live2D HTTP: http://127.0.0.1:{port}/")

        from playwright.async_api import async_playwright
        pw = await async_playwright().start()
        self._browser = await pw.chromium.launch(
            channel='msedge', headless=True,
            args=['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader'])
        self._page = await self._browser.new_page()
        await self._page.set_viewport_size({"width": 240, "height": 320})
        await self._page.goto(f"http://127.0.0.1:{port}/render.html", wait_until='domcontentloaded')

        # 等 Live2D 模型加载完成
        for _ in range(30):
            ready = await self._page.evaluate('() => window._ready')
            if ready: break
            await asyncio.sleep(0.5)

        # 获取模型支持的 motions 和 expressions
        try:
            status = await self._page.evaluate('() => window.getStatus()')
            self._model_motions = status.get('motions', [])
            self._model_expressions = status.get('expressions', [])
            self._model_emotions = status.get('emotions', ['neutral','happy','sad','surprised','angry'])
            print(f"Live2D 模型就绪: motions={self._model_motions}, emotions={self._model_emotions}")
        except Exception as e:
            print(f"Live2D 模型查询失败: {e}")

    # ── 主入口 ──

    async def run(self) -> None:
        self._ctrl_queue = asyncio.Queue()
        print("启动 Live2D 浏览器…")
        await self._init_browser()

        print("启动 Claude SDK…")
        cb = DriverCallbacks(
            on_say=self.on_say,
            on_status=self.on_status,
            on_panel=self.on_panel,
            on_permission=self.on_permission,
            on_log=lambda m: print(f"[claude] {m}"),
        )
        self._driver = ClaudeSdkDriver(cb)
        await self._driver.start()
        print(f"就绪 — 串口: {self.ser.port} @ {self.ser.baudrate} baud\n")

        results = await asyncio.gather(
            self._io_loop(),
            self._read_stdin(),
            return_exceptions=True,
        )
        for r in results:
            if isinstance(r, Exception) and not isinstance(r, asyncio.CancelledError):
                print(f"[bridge] task died: {r}")


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="vbcpet Live2D + Claude Bridge v0.2")
    ap.add_argument("--port", default="COM16")
    ap.add_argument("--baud", type=int, default=2_000_000)
    ap.add_argument("--quality", type=int, default=80)
    args = ap.parse_args()
    bridge = UnifiedBridge(port=args.port, baud=args.baud, jpeg_quality=args.quality)
    asyncio.run(bridge.run())
