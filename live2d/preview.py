"""Live2D 快速预览 — 起本地 HTTP 服务器 + 打开浏览器。

用法:
  python live2d/preview.py            # 自动打开系统浏览器
  python live2d/preview.py --no-open  # 仅打印 URL，手动打开
  python live2d/preview.py --port 8765

VSCode 内置浏览器:
  1. 运行本脚本（不打开系统浏览器）
  2. Ctrl+Shift+P → Simple Browser: Show
  3. 输入 http://127.0.0.1:8765/render.html
"""
from __future__ import annotations

import argparse
import os
import socket
import sys
import threading
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path


class _Handler(SimpleHTTPRequestHandler):
    """修复 .mjs / .wasm MIME 类型。"""
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".mjs": "application/javascript",
        ".wasm": "application/wasm",
    }

    def log_message(self, format, *args) -> None:  # noqa: A002
        print(f"  {self.address_string()} — {format % args}")


def preview(port: int = 8765, open_browser: bool = True) -> None:
    # 切换到 live2d/ 目录
    root = Path(__file__).resolve().parent
    os.chdir(root)

    # 找可用端口
    if port == 0:
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]

    httpd = HTTPServer(("127.0.0.1", port), _Handler)
    url = f"http://127.0.0.1:{port}/render.html"

    print(f"""
╔══════════════════════════════════════════════╗
║        vbcpet Live2D Preview                ║
╠══════════════════════════════════════════════╣
║  Root : {root}
║  URL  : {url}
║  Port : {port}
╠══════════════════════════════════════════════╣
║  Press Ctrl+C to stop                       ║
╚══════════════════════════════════════════════╝
""")

    if open_browser:
        print(f"→ 正在打开浏览器: {url}")
        webbrowser.open(url)

    print("→ HTTP 服务器运行中 (Ctrl+C 停止)...\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n→ 服务器已停止")
        httpd.shutdown()


def main() -> None:
    ap = argparse.ArgumentParser(description="vbcpet Live2D Preview")
    ap.add_argument("--port", type=int, default=8765,
                    help="HTTP 端口 (默认 8765；0=随机)")
    ap.add_argument("--no-open", action="store_true",
                    help="不自动打开浏览器")
    ap.add_argument("--browser", default=None,
                    help="指定浏览器路径 (可选)")
    args = ap.parse_args()

    # 如果指定了浏览器路径，注册之
    if args.browser:
        browser_name = Path(args.browser).stem
        webbrowser.register(browser_name, None, webbrowser.BackgroundBrowser(args.browser))

    preview(port=args.port, open_browser=not args.no_open)


if __name__ == "__main__":
    main()
