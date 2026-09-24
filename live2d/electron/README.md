# vbcpet 桌面桌宠 (Electron 透明窗口宿主 — Route B)

把 `live2d/render.html`（Live2D）、`render_png.html`（PNG 帧序列）、
`render_vrm.html`（VRM 模型）包成一个**透明、无边框、可置顶、可拖动、带托盘**的
Windows 桌面窗口，直接在 PC 桌面上显示角色——无需串口推流到 P4 屏。

## 设计

| 组件 | 职责 |
|------|------|
| `electron/main.js` | 主进程：透明 BrowserWindow + 内置静态服务器 + 托盘 + stdin 事件桥 |
| `electron/preload.js` | 隔离上下文桥：拖动窗口 |
| `electron/package.json` | Electron 依赖与启动脚本 |
| `../render.html` | Live2D 渲染页（`?transparent=1` 透明模式） |
| `../render_png.html` | PNG 帧序列渲染页（大肥鱼轻量） |
| `../render_vrm.html` | VRM/GLB 渲染页（three.js，用户导入模型） |

**核心思路**：渲染完全解耦。桌面版和 P4 推流版共享同一套渲染页和同一套情绪
API（`applyEmotion / setMood / sendMessage / playMotion / setExpression`），
只是「宿主」从 Playwright 无头截图换成 Electron 透明窗口。

## 快速开始

```powershell
cd live2d/electron
npm install          # 安装 electron（首次）
npm start            # Live2D 模式（默认）
npm run start:png    # PNG 帧序列（大肥鱼轻量模式）
npm run start:vrm    # VRM 模型（three.js）
```

启动后角色直接显示在桌面上：按住角色身体可拖动，右键弹出菜单
（置顶/调试/重载/退出），托盘图标可隐藏/显示/退出。

## 参数

```powershell
npx electron . -- --mode vrm --model models/myAvatar.glb --scale 1
npx electron . -- --mode png --size 306 --res 480x800
```

| 参数 | 说明 |
|------|------|
| `--mode live2d` (默认) | Live2D Hiyori |
| `--mode png` | 大肥鱼 PNG 帧序列 |
| `--mode vrm` | VRM/GLB 模型 |
| `--model path` | 模型路径（相对 `live2d/`；vrm 默认 `models/Zome.vrm`，live2d 用 `.model3.json`） |
| `--scale N` | 模型缩放（vrm/live2d 通用） |
| `--res WxH` | 窗口/画布尺寸（默认 480x800） |
| `--size 187\|238\|306` | PNG 精灵尺寸档 |

## 情绪 / 行为驱动（与 P4 推流完全同源）

桌面版暴露与 `render.html` 完全一致的 `window` API，接收
`applyEmotion / setMood / sendMessage / playMotion / setExpression`。外部进程
可用两种方式驱动：

### 1. stdin JSON 事件（一行一个 JSON）

```powershell
echo '{"emotion":"happy","duration":800}' | npx electron . --
echo '{"mood":72}' | npx electron . --
echo '{"message":"我喜欢你"}' | npx electron . --
echo '{"motion":"Idle"}' | npx electron . --
```

`agent_bridge.py` / soullink 事件源可以按这个协议 pipe 进来，
**和串口推流版接的是同一套逻辑**。

### 2. 页面内 IPC（render 内 `ipcRenderer.send('pet:emotion', ...)`）

见 `preload.js` / `main.js` 的 `pet:*` 通道。

## 双显联动（桌面 + P4 硬件同屏）

用统一事件总线把桌面窗口和 P4 串口推流绑在一起，同一个情绪事件同时驱动两个宿主：

```powershell
# 桌面窗口 + P4 推流（三模型任选）
python pc-host/desktop_pet_display.py --mode vrm --model models/myAvatar.glb --port COM16

# 只桌面
python pc-host/desktop_pet_display.py --mode live2d --no-p4
```

详见 `pc-host/desktop_pet_display.py` 头部注释。

## 与其它路线的关系

- **Route A（透明网页壳）**：本目录就是 A 的超集——用 Electron 直接
  加载你的 `render.html`，零改动复用 Cubism 资产。
- **P4 推流版**：`pc-host/stream_simple.py` 仍用 Playwright 截图推流，
  与桌面版共享渲染页，互不影响。
- **MateEngine 参考**（`D:\mateengine\Public.Release.1.9.0b`）：Unity VRM
  桌面伴侣，可借鉴其「桌面常驻 + 透明窗口 + 本地 LLM (Qwen3-4B GGUF)」
  与「自定义 LLM API」的架构。详见 `docs/mateengine-notes.md`。

## 已知注意

- Live2D / VRM 需要 WebGL；Electron 默认硬件加速。若显卡异常可改
  `webPreferences` 或加 `--disable-gpu`（透明窗口 + 软件 WebGL 组合在部分机器
  上可能显示为黑底，届时退回 `render_png.html`）。
- VRM 渲染页默认走 jsDelivr CDN 加载 three.js；离线部署请把
  `three.module.js` + `GLTFLoader.js` 放进 `live2d/vendors/` 并改
  `render_vrm.html` 的 importmap（见 `live2d/models/README.md`）。
- 托盘使用透明占位图标（`nativeImage.createEmpty()`），如需真实图标
  在 `createTray()` 里换成 `nativeImage.createFromPath(...)`。
