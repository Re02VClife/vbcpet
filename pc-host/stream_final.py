'''Minimal Live2D streamer - V1 final board (UART0/CH340 @2M shared link)
Playwright(msedge) JPEG -> FRAME:hexsize + JPEG -> COM
Also relays firmware log lines arriving on the same link as [FW] ...
Usage:
  .venv/Scripts/python pc-host/stream_final.py --port COM8 --baud 2000000 --quality 55
'''
import sys
for _s in (sys.stdout, sys.stderr):
    try: _s.reconfigure(encoding='utf-8', errors='replace')
    except Exception: pass
import argparse, asyncio, socket, threading, time, serial
from pathlib import Path
from http.server import HTTPServer, SimpleHTTPRequestHandler

ap = argparse.ArgumentParser()
ap.add_argument('--port', default='COM8')
ap.add_argument('--baud', type=int, default=2000000)
ap.add_argument('--quality', type=int, default=55)  # keep 480x800 JPEG < 64KB (demux cap)
ap.add_argument('--res', default='480x800')
ap.add_argument('--delay', type=float, default=0.1)
args = ap.parse_args()
W, H = (int(x) for x in args.res.split('x'))
MAX_FRAME = 63 * 1024

class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      '.mjs': 'application/javascript', '.wasm': 'application/wasm'}
    def log_message(self, f, *a): pass
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

def start_http(root):
    import os; os.chdir(root)
    with socket.socket() as s: s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
    httpd = HTTPServer(('127.0.0.1', port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, port

def drain_fw_log(ser, state):
    '''print firmware log lines arriving on the link'''
    try:
        n = ser.in_waiting
        if n <= 0: return
        data = ser.read(n).decode('utf-8', errors='replace')
        state['buf'] += data
        while '\n' in state['buf']:
            line, state['buf'] = state['buf'].split('\n', 1)
            line = line.rstrip('\r')
            if line.strip():
                print('[FW] ' + line[:160], flush=True)
        if len(state['buf']) > 4096: state['buf'] = ''
    except Exception as e:
        print('[FW-read-err] ' + str(e), flush=True)

async def main():
    repo = Path(__file__).resolve().parent.parent
    httpd, port = start_http(str(repo / 'live2d'))
    print(f'http://127.0.0.1:{port}/render.html  viewport={W}x{H} quality={args.quality}', flush=True)

    from playwright.async_api import async_playwright
    pw = await async_playwright().start()
    browser = await pw.chromium.launch(channel='msedge', headless=True,
        args=['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader'])
    page = await browser.new_page()
    await page.set_viewport_size({'width': W, 'height': H})
    await page.goto(f'http://127.0.0.1:{port}/render.html', wait_until='domcontentloaded')
    await asyncio.sleep(2)
    for i in range(30):
        if await page.evaluate('() => window._ready'): break
        await asyncio.sleep(0.5)
    print('Live2D page ready', flush=True)

    ser = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=5)
    print(f'Streaming to {args.port} @ {args.baud} ...', flush=True)

    count = 0; skipped = 0; total_bytes = 0; t0 = time.time()
    fwstate = {'buf': ''}
    while True:
        jpeg = await page.screenshot(type='jpeg', quality=args.quality)
        if len(jpeg) > MAX_FRAME:
            skipped += 1
            if skipped % 10 == 1: print(f'  skip frame {len(jpeg)}B > {MAX_FRAME}B (lower --quality)', flush=True)
            continue
        header = ('FRAME:%08x\n' % len(jpeg)).encode()
        ser.write(header); ser.write(jpeg); ser.flush()
        count += 1; total_bytes += len(jpeg)
        drain_fw_log(ser, fwstate)
        if count % 20 == 0:
            print(f'  Frame {count} ({len(jpeg)}B avg {total_bytes//count}B) fps={count/(time.time()-t0):.1f} skip={skipped}', flush=True)
        await asyncio.sleep(args.delay)

asyncio.run(main())
