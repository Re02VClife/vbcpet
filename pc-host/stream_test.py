"""纯推流压测 — 不加 Claude SDK, 只测串口 I/O 稳定性"""
import asyncio, time, socket, threading
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler
import serial

class _H(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".mjs":"application/javascript",".wasm":"application/wasm"}
    def log_message(self,f,*a):pass
    def end_headers(self):
        self.send_header('Cache-Control','no-cache'); super().end_headers()

def _http(root):
    import os; os.chdir(root)
    with socket.socket() as s: s.bind(("127.0.0.1",0)); port = s.getsockname()[1]
    threading.Thread(target=HTTPServer(("127.0.0.1",port),_H).serve_forever, daemon=True).start()
    return port

async def main():
    repo = Path(__file__).resolve().parent.parent
    port = _http(str(repo / "live2d"))

    from playwright.async_api import async_playwright
    pw = await async_playwright().start()
    browser = await pw.chromium.launch(channel='msedge', headless=True,
        args=['--no-sandbox','--use-gl=angle','--use-angle=swiftshader'])
    page = await browser.new_page()
    await page.set_viewport_size({"width":240,"height":320})
    await page.goto(f"http://127.0.0.1:{port}/render.html", wait_until='domcontentloaded')
    for _ in range(30):
        if await page.evaluate('() => window._ready'): break
        await asyncio.sleep(0.5)

    ser = serial.Serial('COM16', 2000000, timeout=1, write_timeout=5)
    count = 0; t0 = time.time()
    print(f"Streaming to COM16... (Ctrl+C to stop)")
    while True:
        jpeg = await page.screenshot(type='jpeg', quality=80)
        hdr = f"FRAME:{len(jpeg):08x}\n".encode()
        ser.write(hdr); ser.write(jpeg); ser.flush()
        count += 1
        if count % 30 == 0:
            print(f"  {count} frames, {count/(time.time()-t0):.1f} fps, {len(jpeg)//1024}KB", flush=True)
        if ser.in_waiting:
            ser.read(ser.in_waiting)  # 排空 P4 回复防堆积
        await asyncio.sleep(0)

asyncio.run(main())
