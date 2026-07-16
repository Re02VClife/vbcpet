"""Live2D VideoTrack — 无头浏览器渲染 Live2D → 截图 → H.264 → WebRTC 视频轨。

依赖:
  pip install playwright Pillow
  playwright install chromium
  npm install (在 live2d/ 目录)

用法:
  track = Live2DVideoTrack(html_path="live2d/render.html", width=800, height=600)
  pc.addTrack(track)

  动作控制:
  await track.play_motion("TapBody")
  await track.set_expression("Smile", 0.8)
"""
from __future__ import annotations

import asyncio
import logging
import socket
import threading
import time
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from typing import Any

import numpy as np
from aiortc import VideoStreamTrack
from av import VideoFrame

log = logging.getLogger("live2d")

# 动作名映射: Director action → Live2D 标准动作
ACTION_TO_MOTION: dict[str, str] = {
    "idle": "Idle",
    "wave": "TapBody",
    "think": "Idle",
    "nod": "TapBody",
    "celebrate": "TapBody",
    "worry": "Idle",
    "sleep": "Idle",
}

# 表情名称前缀（模型文件中定义的实际表情名）
_EXPRESSION_MAP: dict[str, str] = {
    "neutral": "Neutral",
    "happy": "Happy",
    "sad": "Sad",
    "worried": "Sad",
    "excited": "Happy",
    "angry": "Angry",
    "surprise": "Surprise",
    "fun": "Fun",
}

# 情绪 → 表情 (使用模型真实表情名)
EMOTION_TO_EXPRESSION: dict[str, str] = {
    "neutral": "Neutral",
    "happy": "Happy",
    "sad": "Sad",
    "worried": "Sad",
    "excited": "Happy",
    "angry": "Angry",
    "surprise": "Surprise",
    "fun": "Fun",
}


class Live2DVideoTrack(VideoStreamTrack):
    """从无头 Chromium 页面截图生成 VideoFrame 的 WebRTC 视频轨。"""

    kind = "video"

    def __init__(self, html_path: str = "live2d/render.html",
                 width: int = 800, height: int = 600,
                 fps: int = 30) -> None:
        super().__init__()
        self.html_path = str(Path(html_path).resolve())
        self.width = width
        self.height = height
        self.fps = fps
        self._interval = 1.0 / fps
        self._browser: Any = None
        self._page: Any = None
        self._ready = False
        self._lock = threading.Lock()
        self._last_frame_time = 0.0
        self._n = 0

    # ── 启动 / 停止 ──

    async def start(self) -> None:
        """异步启动 Playwright 无头 Chromium 并加载 render.html。
        起本地 HTTP 服务器绕过 CORS（Edge 禁止 file:// 加载 ESM 模块）。"""
        from playwright.async_api import async_playwright  # noqa: PLC0415

        # 找一个空闲端口起 HTTP 服务器
        self._http_port = 0
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            self._http_port = s.getsockname()[1]

        # HTTP 服务器以 live2d/ 为根目录，修复 .mjs MIME
        import os
        html_dir = str(Path(self.html_path).parent.resolve())
        os.chdir(html_dir)
        class _Handler(SimpleHTTPRequestHandler):
            extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                              ".mjs": "application/javascript",
                              ".wasm": "application/wasm"}
        self._httpd = HTTPServer(("127.0.0.1", self._http_port), _Handler)
        self._httpd_thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._httpd_thread.start()
        log.info("Live2D HTTP: http://127.0.0.1:%d/render.html (root=%s)", self._http_port, html_dir)

        self._pw = await async_playwright().start()
        # 尝试顺序：系统 Chrome → 系统 Edge → Playwright 自带 Chromium
        launch_opts: list[dict] = [
            {"channel": "chrome", "headless": True,
             "args": ["--no-sandbox", "--disable-gpu"]},
            {"channel": "msedge", "headless": True,
             "args": ["--no-sandbox", "--disable-gpu"]},
            {"headless": True,
             "args": ["--no-sandbox", "--disable-gpu", "--disable-software-rasterizer"]},
        ]
        browser = None
        for opts in launch_opts:
            try:
                browser = await self._pw.chromium.launch(**opts)
                log.info("Live2D 浏览器: %s", opts.get("channel", "chromium(bundled)"))
                break
            except Exception as e:
                log.debug("浏览器 %s 不可用: %s", opts.get("channel", "?"), e)
        if not browser:
            raise RuntimeError("无可用浏览器——请安装 Chrome 或 Edge")
        self._browser = browser
        page = await self._browser.new_page()
        await page.set_viewport_size({"width": self.width, "height": self.height})
        page.on("console", lambda msg: log.info("🌐 %s", msg.text))
        page.on("pageerror", lambda err: log.error("🌐 PAGE ERROR: %s", err))
        await page.goto(f"http://127.0.0.1:{self._http_port}/render.html", wait_until="networkidle")

        # 等 PixiJS 加载完成
        for _ in range(30):
            ready = await page.evaluate("() => window._ready")
            if ready:
                break
            await asyncio.sleep(0.5)
        else:
            log.warning("Live2D 模型加载超时，降级显示")

        self._page = page
        self._ready = True
        log.info("Live2D renderer 就绪")

    async def stop(self) -> None:
        self._ready = False
        if self._page:
            await self._page.close()
        if self._browser:
            await self._browser.close()
        if hasattr(self, "_pw") and self._pw:
            await self._pw.stop()
        if hasattr(self, "_httpd") and self._httpd:
            self._httpd.shutdown()

    # ── 动作 / 表情控制 (Director → Live2D) ──

    async def play_motion(self, group: str, index: int = 0) -> bool:
        if not self._page:
            return False
        try:
            return await self._page.evaluate("g => window.playMotion(g)", group)
        except Exception as e:
            log.warning("play_motion %s failed: %s", group, e)
            return False

    async def set_expression(self, name: str, value: float = 1.0) -> bool:
        if not self._page:
            return False
        try:
            return await self._page.evaluate(
                "([n,v]) => window.setExpression(n,v)", [name, value])
        except Exception as e:
            log.warning("set_expression %s failed: %s", name, e)
            return False

    async def apply_avatar(self, action: str, emotion: str, intensity: float = 0.5) -> None:
        """一次性应用动作+表情（由 Director 的 CompressedResult 驱动）。"""
        motion = ACTION_TO_MOTION.get(action, "Idle")
        expr = EMOTION_TO_EXPRESSION.get(emotion, "Neutral")
        await asyncio.gather(
            self.play_motion(motion),
            self.set_expression(expr, intensity),
        )

    # ── VideoStreamTrack 核心 ──

    async def recv(self) -> VideoFrame:
        # 帧率节流
        now = time.time()
        elapsed = now - self._last_frame_time
        if elapsed < self._interval:
            await asyncio.sleep(self._interval - elapsed)
        self._last_frame_time = time.time()
        self._n += 1

        if not self._ready or not self._page:
            # 降级：纯黑帧
            arr = np.zeros((self.height, self.width, 3), dtype=np.uint8)
        else:
            try:
                png = await self._page.screenshot(type="png", clip={
                    "x": 0, "y": 0, "width": self.width, "height": self.height,
                })
                from PIL import Image  # noqa: PLC0415
                arr = np.array(Image.open(__import__("io").BytesIO(png)).convert("RGB"))
            except Exception as e:
                log.error("screenshot failed: %s", e)
                arr = np.zeros((self.height, self.width, 3), dtype=np.uint8)

        # PyAV VideoFrame
        frame = VideoFrame.from_ndarray(arr, format="rgb24")
        pts, time_base = await self.next_timestamp()
        frame.pts = pts
        frame.time_base = time_base
        return frame
