// vbcpet 桌面桌宠 — Electron 主进程 (Route B: 透明无边框窗口宿主)
// 复用 live2d/render.html (Live2D) 或 render_png.html (PNG 帧序列)，
// 通过透明 BrowserWindow 把角色直接贴到 Windows 桌面上。
// 参考: MateEngine (Unity VRM 桌面伴侣) 的"桌面常驻 + 透明窗口 + 托盘"思路,
//       以及 ds-local-pet 大肥鱼 (PySide6 透明 QWidget) 的窗口行为。
'use strict';

const { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage, screen } = require('electron');
const path = require('path');
const http = require('http');
const fs = require('fs');
const net = require('net');

// ── 参数解析 ──
//  --mode live2d|png|vrm   (默认 live2d)
//  --res WxH               (画布分辨率, 默认 480x800)
//  --size 187|238|306      (仅 png 模式精灵尺寸)
//  --model path            (模型路径, 相对 live2d/ 目录; vrm 模式用 .glb/.vrm)
//  --scale N               (模型缩放; vrm/live2d 通用)
//  --with-server           (内置本地 HTTP 服务器供 render.html 加载静态资源)
const ARG = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (a === '--mode') ARG.mode = process.argv[++i];
  else if (a === '--res') ARG.res = process.argv[++i];
  else if (a === '--size') ARG.size = process.argv[++i];
  else if (a === '--model') ARG.model = process.argv[++i];
  else if (a === '--scale') ARG.scale = process.argv[++i];
  else if (a === '--with-server') ARG.withServer = true;
  else if (a === '--transparent') ARG.transparent = true;
}
ARG.mode = ARG.mode || 'live2d';

const LIVE2D_ROOT = path.resolve(__dirname, '..'); // live2d/ 目录

let mainWindow = null;
let tray = null;
let httpPort = 0;

// ── 内置静态文件服务器 (MIME 正确 + no-cache, 复刻 server.py) ──
function startHttpServer(rootDir) {
  const mime = {
    '.html': 'text/html', '.js': 'application/javascript', '.mjs': 'application/javascript',
    '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg',
    '.wasm': 'application/wasm', '.moc3': 'application/octet-stream',
    '.cdi3.json': 'application/json',
    '.glb': 'model/gltf-binary', '.gltf': 'model/gltf+json', '.vrm': 'model/gltf-binary',
    '.bin': 'application/octet-stream', '.ktx2': 'image/ktx2'
  };
  const server = http.createServer((req, res) => {
    let p = decodeURIComponent(req.url.split('?')[0]);
    if (p === '/' || p === '') p = '/index.html';
    const filePath = path.join(rootDir, p);
    const ext = path.extname(filePath).toLowerCase();
    fs.readFile(filePath, (err, data) => {
      if (err) { res.writeHead(404); res.end('Not found'); return; }
      res.writeHead(200, {
        'Content-Type': mime[ext] || 'application/octet-stream',
        'Cache-Control': 'no-cache, no-store, must-revalidate'
      });
      res.end(data);
    });
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => {
      resolve({ server, port: server.address().port });
    });
  });
}

// ── 构建页面 URL ──
function buildUrl(port) {
  const pages = { live2d: 'render.html', png: 'render_png.html', vrm: 'render_vrm.html' };
  const page = pages[ARG.mode] || 'render.html';
  const params = new URLSearchParams();
  params.set('transparent', '1'); // 透明背景 (后面 render.html 读取此参数)
  if (ARG.res) params.set('res', ARG.res);
  if (ARG.size) params.set('size', ARG.size);
  if (ARG.model) params.set('model', ARG.model);
  if (ARG.scale) params.set('scale', ARG.scale);
  return `http://127.0.0.1:${port}/${page}?${params.toString()}`;
}

function createWindow() {
  const { width, height } = screen.getPrimaryDisplay().workAreaSize;

  mainWindow = new BrowserWindow({
    width: 480,
    height: 800,
    transparent: true,
    frame: false,
    resizable: true,
    hasShadow: false,
    alwaysOnTop: false,          // 由托盘菜单切换
    fullscreenable: false,
    skipTaskbar: false,
    backgroundColor: '#00000000',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
      // WebGL 软件渲染兜底 (Live2D PixiJS 需要)
      webgl: true
    }
  });

  // 拖动: 支持按住角色身体拖动窗口 (preload 通过 ipc 触发)
  mainWindow.setAlwaysOnTop(false);
  mainWindow.setPosition(20, height - 820);

  mainWindow.loadURL(buildUrl(httpPort));

  // 打开 DevTools 便于调试 (右键菜单触发)
  mainWindow.webContents.on('context-menu', () => {
    const menu = Menu.buildFromTemplate([
      { label: '切换置顶', click: () => toggleAlwaysOnTop() },
      { label: '打开调试工具', click: () => mainWindow.webContents.openDevTools({ mode: 'detach' }) },
      { label: '重新加载', click: () => mainWindow.webContents.reload() },
      { type: 'separator' },
      { label: '退出桌宠', click: () => app.quit() }
    ]);
    menu.popup();
  });

  mainWindow.on('closed', () => { mainWindow = null; });
}

function toggleAlwaysOnTop() {
  if (!mainWindow) return;
  const cur = mainWindow.isAlwaysOnTop();
  mainWindow.setAlwaysOnTop(!cur);
}

// ── 情绪桥接: 主进程接收外部事件 → 注入 render 页面 ──
// 复用与 stream_simple.py / agent_bridge 完全一致的 API 名,
// 外部可通过 IPC 或 stdin JSON 发 applyEmotion / setMood / sendMessage。
function injectCall(fnName, args) {
  if (!mainWindow || mainWindow.isDestroyed()) return false;
  mainWindow.webContents.executeJavaScript(
    `window['${fnName}'] && window['${fnName}'].apply(window, ${JSON.stringify(args)}); true`
  ).catch(() => false);
  return true;
}

ipcMain.on('pet:drag', (ev, dx, dy) => {
  if (!mainWindow) return;
  const [x, y] = mainWindow.getPosition();
  mainWindow.setPosition(x + dx, y + dy);
});
ipcMain.on('pet:emotion', (ev, name, dur) => injectCall('applyEmotion', [name, dur]));
ipcMain.on('pet:mood', (ev, v) => injectCall('setMood', [v]));
ipcMain.on('pet:message', (ev, text) => injectCall('sendMessage', [text]));
ipcMain.on('pet:quit', () => app.quit());

// ── 标准输入 JSON 事件 (供 agent_bridge 等外部进程 pip 反向驱动) ──
function startStdinBridge() {
  let buf = '';
  process.stdin.setEncoding('utf8');
  process.stdin.on('data', (chunk) => {
    buf += chunk;
    let idx;
    while ((idx = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, idx).trim();
      buf = buf.slice(idx + 1);
      if (!line) continue;
      try {
        const msg = JSON.parse(line);
        if (msg.emotion) injectCall('applyEmotion', [msg.emotion, msg.duration]);
        if (typeof msg.mood === 'number') injectCall('setMood', [msg.mood]);
        if (msg.message) injectCall('sendMessage', [msg.message]);
        if (msg.motion) injectCall('playMotion', [msg.motion]);
        if (msg.expression) injectCall('setExpression', [msg.expression, msg.value]);
      } catch (e) { /* 非 JSON 行忽略 */ }
    }
  });
}

function createTray() {
  // 若无图标则用一个透明 16x16 PNG
  const icon = nativeImage.createEmpty();
  tray = new Tray(icon);
  tray.setToolTip('vbcpet 桌宠');
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: '显示 / 隐藏', click: () => {
        if (!mainWindow) return;
        mainWindow.isVisible() ? mainWindow.hide() : mainWindow.show();
      } },
    { label: '切换置顶', click: () => toggleAlwaysOnTop() },
    { type: 'separator' },
    { label: '模式: ' + ({ live2d: 'Live2D', png: 'PNG 帧序列', vrm: 'VRM' }[ARG.mode] || ARG.mode), enabled: false },
    { label: '退出桌宠', click: () => app.quit() }
  ]));
}

// ── 生命周期 ──
app.whenReady().then(async () => {
  const { server, port } = await startHttpServer(LIVE2D_ROOT);
  httpPort = port;
  createWindow();
  createTray();
  startStdinBridge();
  console.log(`[vbcpet-pet] mode=${ARG.mode} http=http://127.0.0.1:${httpPort}`);
});

app.on('window-all-closed', () => {
  // 桌宠: 关闭窗口即退出 (托盘也在 app.quit)
  app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});
