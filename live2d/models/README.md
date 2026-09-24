# models/ — 用户导入模型目录

把你想在桌宠里显示的模型拖进这个目录，即可在三套渲染管线里加载。

## 已导入模型（来自 MateEngine 源码 `Assets\MATE ENGINE - Avatar`）

| 文件 | 大小 | 格式 | 说明 |
|---|---|---|---|
| `Zome.vrm` | 26 MB | VRM 0.x | **默认模型**（`render_vrm.html` 无 `--model` 时加载），24 组表情，表情最丰富 |
| `aldina.vrm` | 12 MB | VRM 0.x | 17 组表情 |
| `Lazuli_VRM.vrm` | 58 MB | VRM 0.x | 最大，MToon 材质 |

三个模型均为 **VRM 0.x**（`specVersion=0.0`，作者 Yorshka）。`render_vrm.html` 已内置 VRM 0.x 支持：
自动解析 `extensions.VRM.blendShapeMaster` 组合表情，情绪映射 `joy/angry/sorrow/fun`，并做 180° 朝向修正。

### Zome 表情清单（24 组，从 `.vrm` 提取）

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
