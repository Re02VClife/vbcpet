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
import os
import sys
import time
import threading

# Windows 控制台 GBK 打印 emoji/中文会崩 → 强制 UTF-8
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
from http.server import HTTPServer, SimpleHTTPRequestHandler
from itertools import count
from pathlib import Path

import serial


def _project_dir(cwd: str) -> str:
    """Claude Code 项目目录名 (路径 → 连字符)"""
    import re
    p = str(Path(cwd).resolve()).replace("\\", "/").strip("/")
    return re.sub(r"[^A-Za-z0-9]", "-", p)


def _latest_session_id(cwd: str) -> str | None:
    """找 cwd 对应 Claude Code 项目里最近的会话 id"""
    import glob
    files = glob.glob(str(Path.home() / ".claude" / "projects" / _project_dir(cwd) / "*.jsonl"))
    if not files:
        return None
    return Path(max(files, key=os.path.getmtime)).stem

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_driver import DriverCallbacks, Usage, PermissionRequest  # noqa: E402
from agent_bridge import _session_path, inject_to_inputbox, poll_replies, shorten  # noqa: E402
from chat_driver import ChatDriver  # noqa: E402
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
    def __init__(self, port: str = "COM16", baud: int = 2_000_000, jpeg_quality: int = 80,
                 res: str = "480x800", mode: str = "dev",
                 resume_session: str | None = None, fork_session: bool = False):
        self.ser = serial.Serial(port, baud, timeout=0.05, write_timeout=3)
        self.jpeg_quality = jpeg_quality
        self.res = res          # 画布分辨率 "WxH" (默认最终板 480×800; 开发板用 240x320)
        self._mode = mode       # dev=Claude Code CLI (工具/开发), chat=轻量直连 (生活聊天)
        self._resume_session = resume_session   # 显式 resume 指定会话; None=自动用最近会话
        self._fork_session = fork_session       # True=resume 时 fork 新会话 (安全模式)
        self._repo = Path(__file__).resolve().parent.parent   # 项目根 (_start_http 会 chdir, 不能依赖 cwd)
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
        self._mood = 60  # 心情值 0-100, 基线 60
        self._mood_t0 = time.time()

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

    async def _apply_mood(self) -> None:
        """将心情值推送到 Live2D"""
        if not self._page: return
        try:
            await self._page.evaluate(f'() => window.setMood({self._mood})')
        except Exception:
            pass

    async def _adjust_mood(self, delta: int) -> None:
        """调整心情值并同步到 Live2D"""
        self._mood = max(0, min(100, self._mood + delta))
        self._mood_t0 = time.time()
        await self._apply_mood()

    async def _mood_tick(self) -> None:
        """心情随时间缓慢回归基线 60 (每10秒漂移1点)"""
        while True:
            await asyncio.sleep(10)
            if self._mood != 60 and time.time() - self._mood_t0 > 15:
                drift = 1 if self._mood < 60 else -1
                self._mood += drift
                await self._apply_mood()

    async def _apply_l2d_status(self, state: str) -> None:
        """Claude 状态 -> 动作 + 心情偏移 + 情绪表演 (soullink engine)"""
        mood_deltas = {
            "thinking": -2,
            "running":   0,
            "editing":  +3,
            "done":     +8,
            "error":   -15,
            "blocked":  +5,
        }
        delta = mood_deltas.get(state, 0)
        await self._adjust_mood(delta)
        # 状态 → 情绪 (render.html 的 applyEmotion 已接入 soullink engine)
        state_emotions = {
            "thinking": "curious",
            "running":  "neutral",
            "editing":  "happy",
            "done":     "excited",
            "error":    "confused",
            "blocked":  "confused",
        }
        emo = state_emotions.get(state)
        if emo and self._page:
            try:
                await self._page.evaluate(f'() => window.applyEmotion("{emo}")')
            except Exception:
                pass
        # idle 动画由 render.html 持续运行 (mood hook 混合表情)

    async def _set_mode(self, mode: str) -> None:
        """切换对话模式: dev=Claude Code CLI (工具/开发), chat=轻量直连 (生活聊天)"""
        if self._driver:
            try:
                await self._driver.stop()
            except Exception:
                pass
        self._mode = mode
        if mode == "chat":
            print("对话模式 → chat (轻量直连, 无工具, 成本 ~10%)")
            self._driver = ChatDriver(self._cb)
        else:
            resume_id = self._resume_session or _latest_session_id(str(self._repo))
            if resume_id:
                extra = "fork 新会话" if self._fork_session else f"resume {resume_id[:8]}… (带当前上下文)"
                print(f"对话模式 → dev (Claude Code CLI, {extra})")
                self._driver = ClaudeSdkDriver(self._cb, resume_session=resume_id,
                                               fork_session=self._fork_session)
            else:
                print("对话模式 → dev (Claude Code CLI, 新会话)")
                self._driver = ClaudeSdkDriver(self._cb)
        await self._driver.start()

    async def _inject(self, text: str) -> None:
        """中转站: 注入文字到当前 VS Code 对话窗口 → 实时监工 → 推送到硬件"""
        sid = _latest_session_id(str(self._repo))
        if not sid:
            print("[inject] 未找到当前会话 (先开一个 VS Code/Claude Code 窗口)")
            return
        proj = _project_dir(str(self._repo))
        path = _session_path(proj, sid)
        start_off = path.stat().st_size if path.exists() else 0
        print(f"[inject] → VS Code 会话 {sid[:8]}…  : {text[:60]}{'…' if len(text) > 60 else ''}")
        ok = inject_to_inputbox(text)
        if not ok:
            return
        # 流式监工: 线程轮询会话文件, queue 传回主循环
        import queue
        q: queue.Queue = queue.Queue()

        def worker():
            for _off, reply in poll_replies(proj, sid, last_offset=start_off):
                q.put(reply)
            q.put(None)

        threading.Thread(target=worker, daemon=True).start()
        loop = asyncio.get_event_loop()
        replies = 0
        while True:
            item = await loop.run_in_executor(None, q.get)
            if item is None:
                break
            replies += 1
            short = shorten(item)
            self._send_ctrl({"t": "say", "text": short})
            print(f"  [Agent→硬件] {short}")
        print(f"[inject] 结束, 收到 {replies} 条回复")


    # ── AgentDriver 回调 ──

    def on_say(self, text: str) -> None:
        print(f"\n[Claude] {text[:120]}{'…' if len(text) > 120 else ''}")
        # 截断到 ~300 字符 (P4 行缓冲 1024 字节, CJK ~3 字节/字)
        short = text if len(text) <= 300 else text[:297] + "..."
        self._send_ctrl({"t": "say", "text": short})
        # 回复内容 → 情绪分类 (本地 1400 中文语料; 取尾部语义, 端到端情绪链路)
        if self._page:
            tail = text[-60:] if len(text) > 60 else text
            tail = tail.replace("\\", "").replace('"', "'").replace("\n", " ").strip()
            if tail:
                asyncio.ensure_future(
                    self._page.evaluate(f'() => window.sendMessage("{tail}")')
                )

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
            print(f"[P4] 按键: {key}")
            # 硬件按键 → harness 控制 (权限回路 / 状态查询)
            if key in ("yes", "no", "yes_always"):
                allow = key != "no"
                remember = key == "yes_always"
                asyncio.ensure_future(self._resolve_current(allow, remember))
                print(f"  [P4] → harness 权限: {'允许' if allow else '拒绝'}{'+记住' if remember else ''}")
            elif key == "panel":
                info = (f"模式:{self._mode} mood:{self._mood} driver:"
                        + (self._driver.__class__.__name__ if self._driver else "none"))
                print(f"  [P4] panel → {info}")
                self._send_ctrl({"t": "say", "text": f"状态: {self._mode} 模式, 心情 {self._mood}"})
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
                print(f"  [stream] {self._frame_count} frames, {fps:.1f} fps, {len(jpeg)//1024}KB/frame, mood:{self._mood}", flush=True)

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
        print("│ 输入文字 → 发给 AI                          │")
        print("│ [1]=允许  [2]=拒绝  [3]=允许+记住  [4]=面板   │")
        print("│ mood+10 / mood-5 → 调整心情                  │")
        print("│ /mode dev|chat → 切换开发/聊天模式           │")
        print("│ /inject <文字> → 注入当前 VS Code 窗口(中转站)│")
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
                await self._adjust_mood(+5)
            elif line == "2":
                await self._resolve_current(False, False)
                await self._adjust_mood(-3)
            elif line == "3":
                await self._resolve_current(True, True)
                await self._adjust_mood(+5)
            elif line == "4":
                print("[panel] 面板 (v0.2 占位) — 发送 panel 请求到 P4")
                self._send_ctrl({"t": "panel", "tokens": {"input": 0, "output": 0, "cache_read": 0, "total": 0}, "cost_usd": 0.0})
            elif line.startswith("mood") and (line[4:5] in "+-"):
                # mood+10 / mood-5 直接调整心情值
                try:
                    delta = int(line[4:])
                    await self._adjust_mood(delta)
                    print(f"   心情 → {self._mood}")
                except ValueError:
                    print("   用法: mood+10 或 mood-5")
            elif line == "/inject":
                print("   用法: /inject <文字> — 注入到当前 VS Code 对话窗口并监工")
            elif line.startswith("/inject "):
                await self._inject(line[8:].strip())
            elif line == "/mode":
                print(f"   当前模式: {self._mode}  (/mode dev 开发 | /mode chat 聊天)")
            elif line in ("/chat", "/dev"):
                await self._set_mode(line[1:])
                print(f"   → 已切换到 {line[1:]} 模式")
            elif line.startswith("/mode "):
                mode = line[6:].strip()
                if mode in ("dev", "chat"):
                    await self._set_mode(mode)
                    print(f"   → 已切换到 {mode} 模式")
                else:
                    print("   用法: /mode dev | /mode chat")
            else:
                print(f"[user] {line}")
                if self._driver:
                    await self._driver.send_user(line)
                    await self._adjust_mood(+3)  # 交流是正向的

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
        w, h = (int(x) for x in self.res.lower().split("x")[:2])
        self._page = await self._browser.new_page()
        await self._page.set_viewport_size({"width": w, "height": h})
        await self._page.goto(f"http://127.0.0.1:{port}/render.html?res={self.res}",
                              wait_until='domcontentloaded')

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

        self._cb = DriverCallbacks(
            on_say=self.on_say,
            on_status=self.on_status,
            on_panel=self.on_panel,
            on_permission=self.on_permission,
            on_log=lambda m: print(f"[claude] {m}"),
        )
        await self._set_mode(self._mode)
        print(f"就绪 — 串口: {self.ser.port} @ {self.ser.baudrate} baud, 模式: {self._mode}\n")

        results = await asyncio.gather(
            self._io_loop(),
            self._read_stdin(),
            self._mood_tick(),
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
    ap.add_argument("--res", default="480x800",
                    help="画布分辨率 WxH (默认最终板 480x800; 开发板 WT9932P4 用 240x320)")
    ap.add_argument("--mode", default="dev", choices=["dev", "chat"],
                    help="对话模式: dev=Claude Code CLI(开发), chat=轻量直连(生活聊天)")
    ap.add_argument("--resume", default=None,
                    help="dev 模式 resume 指定会话 id (默认自动用最近会话, 带入当前上下文)")
    ap.add_argument("--fork", action="store_true",
                    help="resume 时 fork 新会话 (安全模式, 不写回原会话)")
    args = ap.parse_args()
    bridge = UnifiedBridge(port=args.port, baud=args.baud, jpeg_quality=args.quality,
                           res=args.res, mode=args.mode,
                           resume_session=args.resume, fork_session=args.fork)
    asyncio.run(bridge.run())
