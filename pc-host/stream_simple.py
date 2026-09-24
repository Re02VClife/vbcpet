"""Minimal Live2D / PNG / VRM streamer — no drain thread, direct JPEG

用法:
    python stream_simple.py                    # 默认 live2d 模式 (480x800)
    python stream_simple.py --mode png         # 轻量 PNG 帧序列模式 (大肥鱼)
    python stream_simple.py --mode vrm --model models/my.glb  # VRM 模型
    python stream_simple.py --mode live2d --res 240x320   # 开发板小屏
    python stream_simple.py --mode png --size 187        # 用较小精灵档省带宽
"""
import asyncio, argparse, socket, threading, time, serial
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler

class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "application/javascript", ".wasm": "application/wasm"}
    def log_message(self, f, *a): pass
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

def start_http(root):
    import os; os.chdir(root)
    with socket.socket() as s: s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
    httpd = HTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, port

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', choices=['live2d', 'png', 'vrm'], default='live2d',
                    help='渲染模式: live2d (默认, 高表现力) / png (轻量帧序列) / vrm (VRM 模型)')
    ap.add_argument('--res', default=None, help='画布分辨率 WxH (所有模式均适用)')
    ap.add_argument('--size', default='306', help='PNG 精灵尺寸档 187/238/306 (仅 png)')
    ap.add_argument('--model', default=None, help='模型路径 (相对 live2d/; vrm 模式用 .glb/.vrm)')
    ap.add_argument('--scale', default=None, help='模型缩放倍数')
    ap.add_argument('--port', default='COM16', help='串口设备 (默认 COM16)')
    args = ap.parse_args()

    repo = Path(__file__).resolve().parent.parent
    httpd, port = start_http(str(repo / "live2d"))

    from playwright.async_api import async_playwright
    pw = await async_playwright().start()
    browser_args = ['--no-sandbox']
    if args.mode == 'live2d':
        # Live2D 需要 WebGL 软件渲染
        browser_args += ['--use-gl=angle', '--use-angle=swiftshader']
    elif args.mode == 'vrm':
        # VRM (three.js) 也用 WebGL 软件渲染
        browser_args += ['--use-gl=angle', '--use-angle=swiftshader']
    else:
        browser_args += ['--disable-gpu', '--disable-software-rasterizer']

    browser = await pw.chromium.launch(channel='msedge', headless=True, args=browser_args)
    page = await browser.new_page()

    if args.mode == 'png':
        W, H = 480, 800
        if args.res:
            p = args.res.lower().split('x')
            if len(p) == 2: W, H = int(p[0]), int(p[1])
        url = (f"http://127.0.0.1:{port}/render_png.html?size={args.size}")
        if args.res:
            url += f"&res={args.res}"
    elif args.mode == 'vrm':
        W, H = 480, 800
        if args.res:
            p = args.res.lower().split('x')
            if len(p) == 2: W, H = int(p[0]), int(p[1])
        model = args.model or 'models/Zome.vrm'
        url = (f"http://127.0.0.1:{port}/render_vrm.html?model={model}")
        if args.scale:
            url += f"&scale={args.scale}"
        if args.res:
            url += f"&res={args.res}"
    else:
        W, H = 480, 800
        if args.res:
            p = args.res.lower().split('x')
            if len(p) == 2: W, H = int(p[0]), int(p[1])
        url = f"http://127.0.0.1:{port}/render.html"
        if args.res:
            url += f"?res={args.res}"

    await page.set_viewport_size({"width": W, "height": H})
    await page.goto(url, wait_until='domcontentloaded')
    await asyncio.sleep(2)

    for i in range(30):
        if await page.evaluate('() => window._ready'): break
        await asyncio.sleep(0.5)

    print(f"Streaming mode={args.mode} {W}x{H} to {args.port}...")
    ser = serial.Serial(args.port, 2000000, timeout=1, write_timeout=3)

    quality = 80
    count = 0; t0 = time.time()
    while True:
        jpeg = await page.screenshot(type='jpeg', quality=quality)
        header = f"FRAME:{len(jpeg):08x}\n".encode()
        ser.write(header); ser.write(jpeg); ser.flush()
        count += 1
        if count % 30 == 0:
            print(f"  Frame {count} ({len(jpeg)}B) fps={count/(time.time()-t0):.1f}", flush=True)
        await asyncio.sleep(0)

asyncio.run(main())
