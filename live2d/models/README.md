# models/ — 用户导入模型目录

把你想在桌宠里显示的模型放进这个目录，即可在三套渲染管线里加载。

> ## ⚠️ 本目录不再内置任何模型（2026-09-25 移出仓库）
>
> 原先入库的三个 VRM 角色（合计约 96 MB）**已从 git 中移出**，原因有两条：
>
> 1. **体积**：`Lazuli_VRM.vrm` 单个 57.8 MB，超过 GitHub 推荐的 50 MB 上限；
> 2. **版权**：这些角色版权归原作者 **Yorshka Shop**，MateEngine 的说明里明确
>    「**勿在自建产物里再分发**」，不适合随本仓库分发。
>
> 文件已加入 `.gitignore`（`live2d/models/*.vrm|*.glb|*.gltf`），
> **本地已有的文件不会被删除**，桌宠照常可用；只是不再提交到仓库。

## 一、按需自行放置模型

三个内置过、且已在 `render_vrm.html` / `desktop_pet_display.py` / `stream_simple.py`
里作为默认值出现的角色，都来自开源项目
[Mate-Engine](https://github.com/shinyflvre/Mate-Engine) 的发布包
（`Assets/MATE ENGINE - Avatar/`），从它的公开 Release 里自行取回后放到本目录即可：

| 文件名 | 大小 | 格式 | 来源路径（MateEngine 发布包内） | 说明 |
|---|---|---|---|---|
| `Zome.vrm` | 26 MB | VRM 0.x | `MATE ENGINE - Avatar/Zome.vrm` | **默认模型**（`--model` 缺省时加载），24 组表情，表情最丰富 |
| `aldina.vrm` | 12 MB | VRM 0.x | `MATE ENGINE - Avatar/DLCs/aldina.vrm` | 17 组表情 |
| `Lazuli_VRM.vrm` | 58 MB | VRM 0.x | `MATE ENGINE - Avatar/DLCs/Lazuli_VRM.vrm` | 最大，MToon 材质 |

放置后目录应长这样：

```
live2d/models/
├─ README.md            ← 本文件（会入库）
├─ Zome.vrm             ← 自己放入，不入库
├─ aldina.vrm           （可选）
└─ Lazuli_VRM.vrm       （可选）
```

三个模型均为 **VRM 0.x**（`specVersion=0.0`，作者 Yorshka）。`render_vrm.html` 已内置 VRM 0.x 支持：
自动解析 `extensions.VRM.blendShapeMaster` 组合表情，情绪映射 `joy/angry/sorrow/fun`，并做 180° 朝向修正。

> **没放模型会怎样？** VRM 模式用缺省路径时会加载失败，页面控制台打印
> `VRM load failed: ... (放 .vrm/.glb 到 models/, 或检查路径)`，
> 但不会卡住推流（`window._ready` 仍会置位）。Live2D / PNG 两条管线不受影响，
> 想立刻看效果可以用 `--mode live2d` 或 `--mode png`。

### Zome 表情清单（24 组，从 `.vrm` 提取，供参考）

| preset | 表情组 |
|---|---|
| 核心情绪 | `joy`(笑) `angry`(怒) `sorrow`(哀) `fun`(乐) `neutral` |
| 元音嘴型 | `a i u e o`（A/I/U/E/O） |
| 眨眼 | `blink` `blink_l` `blink_r` |
| 视线 | `lookup` `lookdown` `lookleft` `lookright` |
| 自定义(8) | `star` `heart` `wink` `niyari`(坏笑) `fun2` `sorrow2` `sorrow3` |

`render_vrm.html` 的情绪表已加入自定义项，可直接 `applyEmotion('wink'|'niyari'|'star'|'heart'|'sorrow2'...)`
或 `setExpression('wink', 1.0)` 调用（名称大小写不敏感）。

切换示例（Electron）：
```
cd live2d/electron
npm run start:vrm -- -- --model models/Zome.vrm
```

## 支持的格式

| 格式 | 渲染管线 | 说明 |
|---|---|---|
| `.moc3` + `.model3.json` | Live2D (`render.html`) | Cubism 4 模型，参照 `Hiyori/` 目录结构（`.model3.json` + `.moc3` + 纹理 + motions/expressions） |
| `.glb` / `.gltf` | VRM (`render_vrm.html`) | GLTF/GLB 二进制，含 blendshape（表情）最佳；VRM 0.x 也可放（会被 GLTFLoader 当作 glb 尝试，见下方说明） |
| `.vrm` | VRM (`render_vrm.html`) | VRM 1.0 需 @pixiv/three-vrm 加载器（见下文）；纯 VRM 0.x 若为 glb 容器可直接改后缀为 `.glb` |

> `.vrm` / `.glb` / `.gltf` 已在 `.gitignore` 中；想入库自己的模型请自行调整规则，
> 并确认该模型许可允许再分发。

### PNG 帧序列
大肥鱼 PNG 帧序列在 `assets/dafeiyu/`，不走 `models/` 目录；若要自定义 PNG 角色，把 sprite 图集按
`inventory.json` 的 `states/<state>/frames/<size>` 结构放入即可（详见 `render_png.html` 注释）。

## 使用示例

### Live2D
```
# 把 ModelA 目录 (含 ModelA.model3.json) 放进 live2d/models/ 后：
python pc-host/desktop_pet_display.py --mode live2d --model models/ModelA/ModelA.model3.json
```

### VRM / GLB
```
python pc-host/desktop_pet_display.py --mode vrm --model models/myAvatar.glb
python pc-host/desktop_pet_display.py --mode vrm --model models/myAvatar.glb --scale 1.2
```

Electron 单侧：
```
cd live2d/electron
npm run start:vrm -- -- --model models/myAvatar.glb
```

### 提取模型元数据 / 表情清单
```
node tools/extract_vrm_meta.mjs live2d/models/Zome.vrm
```
用来核对任意模型的 `meta`（作者/许可/版本）与 `blendShapeMaster` 表情组，
换新角色时先跑一遍，再决定要不要改 `render_vrm.html` 的 `EMOTION_MORPHS` 映射。

## VRM 1.0 完整支持（已内置，自动降级）

`render_vrm.html` 现在**优先加载 `@pixiv/three-vrm`（完整 VRM 1.0 规范）**，包括：
- **springbone**：弹簧头发/裙子等二次物理（每帧 `vrm.update(dt)`）
- **lookAt**：视线跟踪（`vrm.lookAt.target = camera`，视线跟随相机）
- **blink**：眨眼
- **expressionManager**：VRM 标准表情 blendshape（Joy/Angry/Sorrow/Surprised/happy/sad 等）重定向

加载策略（自动降级，不破坏现有功能）：
1. **联网**：走 jsDelivr CDN（importmap 已配 `@pixiv/three-vrm@3.4.2`），零安装。
2. **离线**：`cd live2d && npm install three @pixiv/three-vrm`，然后把
   `render_vrm.html` importmap 里的：
   ```
   "@pixiv/three-vrm": "https://cdn.jsdelivr.net/npm/@pixiv/three-vrm@3.4.2/lib/three-vrm.module.min.js"
   ```
   换成本地路径 `"./node_modules/@pixiv/three-vrm/lib/three-vrm.module.js"`。
3. **无 three-vrm 时**：自动回退 `GLTFLoader`（纯 glb/gltf + blendshape，无 springbone/lookAt）。

> 注意：CDN 版 three-vrm 依赖 `three@0.160.0`（importmap 已匹配）；本地 npm 安装时
> 也请保持 three 版本一致（`npm install three@0.160.0 @pixiv/three-vrm`）。

## 表情映射说明

三套管线都暴露统一 API（`applyEmotion / setMood / sendMessage / playMotion / setExpression`）：

- **Live2D**：情绪 → `EMOTION_PRESETS` 参数表（soullink 引擎驱动时走 VAD/FACS）。
- **PNG**：情绪 → `EMOTION_TO_STATE` 大肥鱼状态帧。
- **VRM**：情绪 → `EMOTION_MORPHS` blendshape（Joy/Angry/Sorrow/Surprised，VRM 标准命名）。

导入自定义模型时，如你的 blendshape 命名不同，编辑 `render_vrm.html` 顶部的 `EMOTION_MORPHS` 映射即可。
