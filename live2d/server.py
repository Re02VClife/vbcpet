"""Minimal HTTP server for Live2D static files with correct MIME types."""
import http.server
import os
import socket
import sys

class H(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.mjs': 'application/javascript', '.wasm': 'application/wasm'}
    def log_message(self, fmt, *args):
        pass  # silent
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

os.chdir(os.path.dirname(os.path.abspath(__file__)))
with socket.socket() as s:
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]

# Write port to file so we can find it
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '.server_port'), 'w') as f:
    f.write(str(port))

print(f'Live2D HTTP: http://127.0.0.1:{port}/render.html', file=sys.stderr, flush=True)
print(f'Tuning tool: http://127.0.0.1:{port}/tune_expressions.html', file=sys.stderr, flush=True)
sys.stderr.flush()
http.server.HTTPServer(('127.0.0.1', port), H).serve_forever()
