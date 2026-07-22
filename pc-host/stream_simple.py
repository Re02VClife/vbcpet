"""Minimal Live2D streamer — no drain thread, direct JPEG"""
import asyncio, socket, threading, time, serial
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
    repo = Path(__file__).resolve().parent.parent
    httpd, port = start_http(str(repo / "live2d"))
    
    from playwright.async_api import async_playwright
    pw = await async_playwright().start()
    browser = await pw.chromium.launch(channel='msedge', headless=True,
        args=['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader'])
    page = await browser.new_page()
    await page.set_viewport_size({"width": 240, "height": 320})
    await page.goto(f"http://127.0.0.1:{port}/render.html", wait_until='domcontentloaded')
    await asyncio.sleep(2)
    
    for i in range(30):
        if await page.evaluate('() => window._ready'): break
        await asyncio.sleep(0.5)
    
    print(f"Streaming to COM16...")
    ser = serial.Serial('COM16', 2000000, timeout=1, write_timeout=3)
    
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
