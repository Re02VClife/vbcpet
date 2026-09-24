"""desktop_pet_display.py — 双显联动中枢 (Electron 桌面窗口 + P4 串口推流)

把「同一个模型事件源」广播到两个宿主：
  1. Electron 透明桌面窗口 (live2d/electron，桌面常驻桌宠)
  2. P4 硬件屏幕 (live2d/render*.html → 无头截图 → JPEG → USB 串口)

两者共享同一套 window API (applyEmotion / setMood / sendMessage / playMotion /
setExpression / getStatus / getEmotionState)，因此 soullink / agent_bridge / ASR 等
任何一处产生的情绪/消息事件，都通过本模块的事件总线同时驱动桌面 + 硬件，实现「双显」。

三种渲染模型自由切换 / 用户导入：
  --mode live2d   render.html (Hiyori .moc3, 高表现力, 默认)
  --mode png      render_png.html (大肥鱼 PNG 帧序列, 轻量)
  --mode vrm      render_vrm.html (用户导入的 .glb/.vrm, three.js)
  --model path    用户导入模型路径 (相对 live2d/ 目录)

用法:
  # 只桌面 (Electron 双显联动的一侧)
  python desktop_pet_display.py --mode vrm --model models/myAvatar.glb

  # 桌面 + P4 双显
  python desktop_pet_display.py --mode live2d --port COM16

  # 只 P4 推流 (等价于 stream_simple.py)
  python desktop_pet_display.py --no-desktop --mode png --port COM16

设计: 本模块是「事件消费者」—— 它暴露 send_emotion() / send_message() /
send_motion() 等接口，被 director.py / agent_bridge / soullink 事件源调用；
同时也监听 stdin JSON 行，供外部进程 pip 反向驱动 (与 electron main.js 的
startStdinBridge 契约一致)。
"""
from __future__ import annotations

import argparse
import asyncio
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

# 控制台 UTF-8 (Windows GBK 下打印中文/emoji 防崩)
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

REPO = Path(__file__).resolve().parent.parent
LIVE2D_ROOT = REPO / "live2d"
ELECTRON_DIR = LIVE2D_ROOT / "electron"

_MODE_PAGE = {
    "live2d": "render.html",
    "png": "render_png.html",
    "vrm": "render_vrm.html",
}


# ─────────────────────────────────────────────
# 事件总线 (单进程内广播)
# ─────────────────────────────────────────────
class EventBus:
    """把事件扇出到已注册的宿主 (桌面 Electron + P4 推流 host)。"""

    def __init__(self) -> None:
        self._hosts: list[object] = []

    def register(self, host: object) -> None:
        if host not in self._hosts:
            self._hosts.append(host)

    def emit(self, method: str, *args) -> None:
        for h in self._hosts:
            fn = getattr(h, method, None)
            if fn:
                try:
                    fn(*args)
                except Exception as e:  # noqa: BLE001
                    print(f"[bus] {method} -> {type(h).__name__} 失败: {e}")

    # —— 语义化接口 (供 director / agent_bridge / soullink 调用) ——
    def emotion(self, name: str, duration: float = 800) -> None:
        self.emit("apply_emotion", name, duration)

    def mood(self, value: float) -> None:
        self.emit("set_mood", value)

    def message(self, text: str) -> None:
        self.emit("send_message", text)

    def motion(self, name: str) -> None:
        self.emit("play_motion", name)

    def expression(self, name: str, value: float) -> None:
        self.emit("set_expression", name, value)


# ─────────────────────────────────────────────
# 宿主 A: Electron 桌面窗口
# ─────────────────────────────────────────────
class DesktopHost:
    """在 Electron 透明窗口里跑模型 (桌面常驻桌宠)。

    通过子进程 stdin (JSON 行) 向 electron main.js 的 startStdinBridge 注入事件；
    契约: 每行一个 JSON 对象，字段 emotion/mood/message/motion。
    """

    def __init__(self, mode: str, res: str | None, size: str | None,
                 model: str | None, scale: str | None) -> None:
        self._proc: subprocess.Popen | None = None

        cmd = ["npm", "start", "--", "--", f"--mode={mode}"]
        if res:
            cmd.append(f"--res={res}")
        if size:
            cmd.append(f"--size={size}")
        if model:
            cmd.append(f"--model={model}")
        if scale:
            cmd.append(f"--scale={scale}")

        # 优先用本地 electron，未装则回退 npx（会联网下载）
        if not (ELECTRON_DIR / "node_modules").exists():
            print("[desktop] electron 依赖未安装，首次运行将执行 npm install…")
            subprocess.run(["npm", "install"], cwd=ELECTRON_DIR, check=False)

        print("[desktop] 启动 Electron:", " ".join(cmd))
        # Windows 下隐藏控制台窗口
        startinfo = None
        if os.name == "nt":
            startinfo = subprocess.STARTUPINFO()
            startinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW

        self._proc = subprocess.Popen(
            cmd, cwd=ELECTRON_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8",
            startupinfo=startinfo,
        )

    def _send(self, obj: dict) -> None:
        if self._proc and self._proc.stdin:
            try:
                self._proc.stdin.write(json.dumps(obj, ensure_ascii=False) + "\n")
                self._proc.stdin.flush()
            except Exception as e:  # noqa: BLE001
                print(f"[desktop] 注入失败: {e}")

    def apply_emotion(self, name: str, duration: float = 800) -> None:
        self._send({"emotion": name, "duration": duration})

    def set_mood(self, value: float) -> None:
        self._send({"mood": value})

    def send_message(self, text: str) -> None:
        self._send({"message": text})

    def play_motion(self, name: str) -> None:
        self._send({"motion": name})

    def set_expression(self, name: str, value: float) -> None:
        # electron main.js 当前 stdin 桥未处理 expression，转发为 emotion 语义
        self._send({"expression": name, "value": value})


# ─────────────────────────────────────────────
# 宿主 B: P4 串口推流
# ─────────────────────────────────────────────
class P4StreamHost:
    """无头浏览器截图 → JPEG → USB 串口推给 P4 硬件屏。

    复用 stream_simple.py 的截图推流逻辑，但改为：
    - 接收事件总线的事件 → page.evaluate 调用窗口 API 驱动模型；
    - 在独立线程跑 asyncio 循环。
    """

    def __init__(self, mode: str, res: str | None, size: str | None,
                 model: str | None, scale: str | None, port: str) -> None:
        self.mode = mode
        self.res = res
        self.size = size
        self.model = model
        self.scale = scale
        self.port = port
        self._loop: asyncio.AbstractEventLoop | None = None
        self._page = None
        self._thread: threading.Thread | None = None
        self._ready = threading.Event()

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._loop.run_until_complete(self._stream())

    async def _stream(self) -> None:
        import socket
        from http.server import HTTPServer, SimpleHTTPRequestHandler
        import serial

        class Handler(SimpleHTTPRequestHandler):
            extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                              ".mjs": "application/javascript", ".wasm": "application/wasm",
                              ".glb": "model/gltf-binary", ".vrm": "model/gltf-binary"}
            def log_message(self, f, *a): pass
            def end_headers(self):
                self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
                super().end_headers()

        # 本地静态服务器 (供 render 页加载资产)
        os.chdir(LIVE2D_ROOT)
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            http_port = s.getsockname()[1]
        httpd = HTTPServer(("127.0.0.1", http_port), Handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()

        # 页面 URL
        page = _MODE_PAGE[self.mode]
        params = []
        if self.mode == "png" and self.size:
            params.append(f"size={self.size}")
        if self.mode == "vrm":
            params.append(f"model={self.model or 'models/Zome.vrm'}")
            if self.scale:
                params.append(f"scale={self.scale}")
        if self.res:
            params.append(f"res={self.res}")
        url = f"http://127.0.0.1:{http_port}/{page}"
        if params:
            url += "?" + "&".join(params)

        W, H = 480, 800
        if self.res:
            p = self.res.lower().split("x")
            if len(p) == 2:
                W, H = int(p[0]), int(p[1])

        from playwright.async_api import async_playwright
        pw = await async_playwright().start()
        browser_args = ["--no-sandbox"]
        if self.mode in ("live2d", "vrm"):
            browser_args += ["--use-gl=angle", "--use-angle=swiftshader"]
        else:
            browser_args += ["--disable-gpu", "--disable-software-rasterizer"]

        browser = await pw.chromium.launch(channel="msedge", headless=True, args=browser_args)
        self._page = await browser.new_page()
        await self._page.set_viewport_size({"width": W, "height": H})
        await self._page.goto(url, wait_until="domcontentloaded")
        await asyncio.sleep(2)
        for _ in range(30):
            if await self._page.evaluate("() => window._ready"):
                break
            await asyncio.sleep(0.5)

        print(f"[p4] 推流 {self.mode} {W}x{H} -> {self.port}")
        ser = serial.Serial(self.port, 2000000, timeout=1, write_timeout=3)
        self._ready.set()

        quality = 80
        count = 0
        t0 = time.time()
        while True:
            jpeg = await self._page.screenshot(type="jpeg", quality=quality)
            header = f"FRAME:{len(jpeg):08x}\n".encode()
            ser.write(header); ser.write(jpeg); ser.flush()
            count += 1
            if count % 30 == 0:
                print(f"[p4] Frame {count} ({len(jpeg)}B) fps={count/(time.time()-t0):.1f}", flush=True)
            await asyncio.sleep(0)

    # —— 事件总线接口 (在推流线程的事件循环上执行 JS) ——
    def _js(self, expr: str) -> None:
        if not self._loop or not self._page:
            return
        async def _do():
            try:
                await self._page.evaluate(expr)
            except Exception as e:  # noqa: BLE001
                print(f"[p4] evaluate 失败: {e}")
        asyncio.run_coroutine_threadsafe(_do(), self._loop)

    def apply_emotion(self, name: str, duration: float = 800) -> None:
        self._js(f"window.applyEmotion && window.applyEmotion({json.dumps(name)}, {duration})")

    def set_mood(self, value: float) -> None:
        self._js(f"window.setMood && window.setMood({value})")

    def send_message(self, text: str) -> None:
        self._js(f"window.sendMessage && window.sendMessage({json.dumps(text)})")

    def play_motion(self, name: str) -> None:
        self._js(f"window.playMotion && window.playMotion({json.dumps(name)})")

    def set_expression(self, name: str, value: float) -> None:
        self._js(f"window.setExpression && window.setExpression({json.dumps(name)}, {value})")


# ─────────────────────────────────────────────
# stdin 事件源 (外部进程 pip 反向驱动, 契约同 electron)
# ─────────────────────────────────────────────
def start_stdin_source(bus: EventBus) -> None:
    def _read():
        buf = ""
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            _dispatch(msg, bus)
    threading.Thread(target=_read, daemon=True).start()


def _dispatch(msg: dict, bus: EventBus) -> None:
    """把一条事件 dict 分发到总线 (stdin + relay 共用)。"""
    # message 事件里的 emotion 是「附着在对话上的分类」，随 message 一起分发；
    # 独立的 emotion 事件 (无 message) 才走顶层 emotion 分支，避免重复。
    if msg.get("message"):
        if msg.get("emotion"):
            bus.emotion(msg["emotion"], 600)
        bus.message(msg["message"])
    elif msg.get("emotion"):
        bus.emotion(msg["emotion"], msg.get("duration", 800))
    if "mood" in msg:
        bus.mood(msg["mood"])
    if msg.get("motion"):
        bus.motion(msg["motion"])
    if msg.get("expression"):
        bus.expression(msg["expression"], msg.get("value", 0.0))


def start_relay_source(bus: EventBus) -> None:
    """从进程内 emotion_relay.relay 订阅事件 (agent_bridge/serial_bridge/director
    在同进程内产生「对话→情绪」时直接驱动双显)。失败则静默跳过 (未安装中继)。"""
    try:
        from emotion_relay import relay  # noqa: PLC0415
    except Exception:  # noqa: BLE001
        print("[hub] emotion_relay 不可用，跳过进程内事件源 (仅 stdin)。")
        return
    relay.subscribe(lambda ev: _dispatch(ev, bus))
    print("[hub] 已订阅进程内 emotion_relay 事件源。")


# ─────────────────────────────────────────────
def main() -> None:
    ap = argparse.ArgumentParser(description="双显联动中枢: Electron 桌面 + P4 推流")
    ap.add_argument("--mode", choices=["live2d", "png", "vrm"], default="live2d")
    ap.add_argument("--res", default=None, help="画布分辨率 WxH")
    ap.add_argument("--size", default=None, help="PNG 精灵尺寸 187/238/306")
    ap.add_argument("--model", default=None, help="模型路径 (相对 live2d/; vrm/live2d)")
    ap.add_argument("--scale", default=None, help="模型缩放")
    ap.add_argument("--port", default="COM16", help="P4 串口设备")
    ap.add_argument("--no-desktop", action="store_true", help="只推流 P4, 不启动桌面窗口")
    ap.add_argument("--no-p4", action="store_true", help="只桌面, 不推流 P4")
    args = ap.parse_args()

    bus = EventBus()

    desktop_host = None
    if not args.no_desktop:
        desktop_host = DesktopHost(args.mode, args.res, args.size, args.model, args.scale)
        bus.register(desktop_host)

    p4_host = None
    if not args.no_p4:
        p4_host = P4StreamHost(args.mode, args.res, args.size, args.model, args.scale, args.port)
        p4_host.start()
        bus.register(p4_host)

    start_stdin_source(bus)
    start_relay_source(bus)

    print("[hub] 双显联动就绪。事件源：in-process relay / stdin JSON 行 / 直接调用 bus 接口。")
    print('[hub] 示例: echo \'{"emotion":"happy"}\' | python desktop_pet_display.py …')
    if not args.no_desktop:
        print("[hub] 桌面窗口 (Electron) + P4 推流已启动，Ctrl+C 退出。")
    else:
        print("[hub] P4 推流已启动，Ctrl+C 退出。")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("[hub] 退出。")


if __name__ == "__main__":
    main()
