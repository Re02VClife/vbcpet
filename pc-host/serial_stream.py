"""serial_stream.py — PC Live2D 渲染 → JPEG → USB 串口推流到 P4

用法:
  python pc-host/serial_stream.py --port COM16 --baud 2000000
  python pc-host/serial_stream.py --port COM16 --baud 2000000 --fps 15 --quality 55

依赖: pip install pyserial playwright Pillow
"""
from __future__ import annotations

import argparse
import asyncio
import io
import logging
import socket
import sys
import threading
import time
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

import serial

log = logging.getLogger("serial_stream")

class _Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "application/javascript",
                      ".wasm": "application/wasm"}
    def log_message(self, format, *args) -> None:
        log.info("HTTP %s — %s", self.address_string(), format % args)
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

def _start_http_server(root: Path, port: int = 0) -> tuple[HTTPServer, int]:
    import os
    os.chdir(root)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0 if port == 0 else port))
        port = s.getsockname()[1]
    httpd = HTTPServer(("127.0.0.1", port), _Handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    log.info("Live2D HTTP: http://127.0.0.1:%d/ (root=%s)", port, root)
    return httpd, port

async def _start_browser(width: int = 240, height: int = 320) -> tuple:
    from playwright.async_api import async_playwright
    pw = await async_playwright().start()
    opts_list = [
        {"channel": "chrome", "headless": True,
         "args": ["--no-sandbox", "--use-gl=angle", "--use-angle=swiftshader"]},
        {"channel": "msedge", "headless": True,
         "args": ["--no-sandbox", "--use-gl=angle", "--use-angle=swiftshader"]},
        {"headless": True,
         "args": ["--no-sandbox", "--use-gl=angle", "--use-angle=swiftshader"]},
    ]
    browser = None
    for opts in opts_list:
        try:
            browser = await pw.chromium.launch(**opts)
            log.info("浏览器: %s", opts.get("channel", "chromium(bundled)"))
            break
        except Exception as e:
            log.debug("不可用: %s — %s", opts.get("channel", "?"), e)
    if not browser:
        raise RuntimeError("请安装 Chrome 或 Edge")
    page = await browser.new_page()
    await page.set_viewport_size({"width": width, "height": height})
    page.on("pageerror", lambda err: log.error("PAGE ERROR: %s", err))
    return pw, browser, page

async def capture_frame(page, quality: int = 55) -> bytes:
    from PIL import Image
    png = await page.screenshot(type="png")
    img = Image.open(io.BytesIO(png)).convert("RGB")
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=quality)
    return buf.getvalue()

def send_frame(ser: serial.Serial, jpeg_data: bytes) -> None:
    header = f"FRAME:{len(jpeg_data):08x}\n".encode()
    ser.write(header)
    ser.write(jpeg_data)
    ser.flush()

async def stream_loop(ser: serial.Serial, http_port: int, html_path: str,
                      width: int, height: int, fps: int, quality: int) -> None:
    url = f"http://127.0.0.1:{http_port}/{html_path}?res={width}x{height}"
    log.info("加载: %s (%dx%d) fps=%d quality=%d", url, width, height, fps, quality)

    pw, browser, page = await _start_browser(width, height)
    # 监听浏览器控制台输出，帮助调试
    page.on("console", lambda msg: log.info("[browser] %s", msg.text))
    await page.goto(url, wait_until="domcontentloaded")
    await asyncio.sleep(1)  # 等 JS 初始化

    for i in range(30):
        ready = await page.evaluate("() => window._ready")
        if ready:
            status = await page.evaluate("() => window.getStatus()")
            log.info("页面就绪: %s", status)
            # 检查实际页面标题
            title = await page.title()
            log.info("页面标题: %s", title)
            break
        await asyncio.sleep(0.5)
    else:
        log.warning("页面加载超时")

    interval = 1.0 / fps
    frame_count = 0
    total_bytes = 0
    t0 = time.time()
    log.info("开始推流 fps=%d quality=%d Ctrl+C 停止", fps, quality)

    frame_count = 0
    total_bytes = 0

    try:
        while True:
            t_start = time.time()
            jpeg = await capture_frame(page, quality)
            send_frame(ser, jpeg)
            frame_count += 1
            total_bytes += len(jpeg)

            # P4 解码+显示需要时间，帧间等待避免 USB 缓冲区溢出
            await asyncio.sleep(0.25)

            # 每帧打印进度
            elapsed_total = time.time() - t0
            print(f"\r  帧 #{frame_count}  JPEG={len(jpeg):5d}B  "
                  f"fps={frame_count / elapsed_total:.1f}  "
                  f"BW={total_bytes / elapsed_total / 1024:.1f}KB/s  ", end="")
            sys.stdout.flush()

            if frame_count % 30 == 0:
                print()  # 换行

            elapsed = time.time() - t_start
            if elapsed < interval:
                await asyncio.sleep(interval - elapsed)
    except (asyncio.CancelledError, KeyboardInterrupt):
        pass
    finally:
        log.info("停止: %d 帧", frame_count)
        await browser.close()
        await pw.stop()

def main() -> None:
    ap = argparse.ArgumentParser(description="vbcpet Live2D serial streamer")
    ap.add_argument("--port", default="COM16", help="串口号")
    ap.add_argument("--baud", type=int, default=2000000, help="波特率")
    ap.add_argument("--fps", type=int, default=12, help="目标帧率")
    ap.add_argument("--quality", type=int, default=55, help="JPEG 质量 1-100")
    ap.add_argument("--width", type=int, default=480, help="截图宽度 (默认最终板 480×800; 开发板用 240)")
    ap.add_argument("--height", type=int, default=800, help="截图高度 (默认最终板 800; 开发板用 320)")
    ap.add_argument("--live2d-dir", default="live2d", help="Live2D 渲染根目录")
    ap.add_argument("--html", default="render.html", help="HTML 文件名")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")

    repo = Path(__file__).resolve().parent.parent
    live2d_root = (repo / args.live2d_dir).resolve()
    if not live2d_root.is_dir():
        log.error("Live2D 目录不存在: %s", live2d_root)
        sys.exit(1)

    log.info("打开串口 %s @ %d baud...", args.port, args.baud)
    ser = serial.Serial(args.port, args.baud, timeout=1, write_timeout=3)

    # 后台线程排空接收缓冲 + 打印调试信息
    def _drain():
        while True:
            try:
                data = ser.read(4096)
                if data:
                    # 尝试解码为文本调试信息
                    try:
                        text = data.decode('ascii', errors='replace').strip()
                        if text:
                            log.info("[P4] %s", text)
                    except Exception:
                        pass
            except Exception:
                break
    import threading
    t = threading.Thread(target=_drain, daemon=True)
    t.start()

    httpd, http_port = _start_http_server(live2d_root)

    try:
        asyncio.run(stream_loop(ser, http_port, args.html,
                                args.width, args.height,
                                args.fps, args.quality))
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        httpd.shutdown()
        log.info("已停止")

if __name__ == "__main__":
    main()
