# 参考的开源项目索引

> 目的:记录 vbcpet 开发中**参考 / 采用 / 计划参考**的开源项目,方便回溯来源、核对许可、定位用法。
> 新增项目直接追加到对应小节,每个项目按下方字段填写;填写后我会在后续开发中主动参考它们。

## 总览

| 项目 | 仓库 | 用途 | 状态 |
|------|------|------|------|
| Hiyori 模型 | [Live2D/CubismWebSamples](https://github.com/Live2D/CubismWebSamples) | 骨架/动作/表情基础 | ✅ 已用 |
| @jannchie/pixi-live2d-display | [jannchie/pixi-live2d-display](https://github.com/jannchie/pixi-live2d-display) | Live2D 渲染引擎 (PixiJS v8) | ✅ 已用 |
| pixi.js | [pixijs/pixijs](https://github.com/pixijs/pixijs) | WebGL 渲染框架 | ✅ 已用 |
| Live2D CubismCore | 官方运行时 (闭源 lib) | 模型解析/绘制核心 | ✅ 已用 |
| | | | ⬜ 待填 |

---

## 一、已采用(当前项目正在用)

### Hiyori 模型 (Live2D 官方示例)

- **仓库**: [Live2D/CubismWebSamples](https://github.com/Live2D/CubismWebSamples)(来自官方 Cubism SDK Samples)
- **许可**: Live2D 官方样例模型,使用遵循 [Live2D 许可协议](https://www.live2d.com/eula/live2d_open_software_license_agreement_en.html)
- **版本**: Cubism 3 格式(`.moc3` + model3/physics3/pose3/cdi3/userdata3.json)
- **用途**: 模型骨架、动作、表情的基础形象
- **在本项目的用法**:
  - `live2d/Hiyori/` 是本地副本(从 CubismWebSamples clone,`.git` 被 [.gitignore](../.gitignore) 排除)
  - `render.html` 直接加载 `Hiyori.model3.json`,通过 `expressions/` + `motions/` 驱动表情/动作
  - 贴图 `Hiyori.2048/`(两张 2048×2048)是换皮工具链的输入(见 `docs/live2d-comfyui.md`)
- **备注**: 最终会被用户自绘形象替换,但骨架/网格保留

### @jannchie/pixi-live2d-display

- **仓库**: [jannchie/pixi-live2d-display](https://github.com/jannchie/pixi-live2d-display)([npm](https://www.npmjs.com/package/@jannchie/pixi-live2d-display))
- **来源**: [guansss/pixi-live2d-display](https://github.com/guansss/pixi-live2d-display) 的维护 fork,原版只支持 PixiJS v6/v7,此 fork 适配 **PixiJS v8**
- **许可**: MIT(原版 MIT;fork 以 npm 包为准)
- **版本**: `^1.3.0`(已装;最新 1.4.0)
- **用途**: Live2D 模型加载/播放/交互,统一了 Cubism 2/3/4 的复杂 API
- **在本项目的用法**: `live2d/render.html` 里 `Live2DModel.from(MODEL_PATH)` 加载模型;配合 PIXI offscreen canvas 截图做 WebRTC/串口推流视频源
- **备注**: 换 PixiJS 大版本时此 fork 是唯一 v8 兼容路线,不要降回原版

### pixi.js

- **仓库**: [pixijs/pixijs](https://github.com/pixijs/pixijs)
- **许可**: MIT
- **版本**: `^8.0.0`
- **用途**: WebGL 渲染框架,pixi-live2d-display 的渲染底座
- **在本项目的用法**: `render.html` 的 `PIXI.Application` + offscreen 截图(全头less,不依赖可见窗口)

### Live2D CubismCore

- **仓库**: 官方运行时,通过 [Cubism Web SDK](https://www.live2d.com/en/products/cubism-sdk) 获取(仓库内 `live2d/live2dcubismcore.min.js` 是 CDN 拷贝)
- **许可**: 闭源,免费使用但受 Live2D 条款约束
- **版本**: Cubism 4 运行时(向后兼容 Cubism 3 模型)
- **用途**: `.moc3` 的解析 + WebGL 绘制核心
- **在本项目的用法**: render.html 里先于 `pixi-live2d-display` 全局加载

---

## 二、用户新增参考项目

> 每个项目占一个小节,复制下面的模板填写。字段说明:
> - **许可/版本**: 由我调研后补全,你不需要填

### 模板

```
### [项目名]

- **仓库**: <URL>
- **许可**: <MIT / Apache-2.0 / GPL…>
- **版本/commit**: <版本号 或 commit hash>
- **用途**: <一句话>
- **在本项目的用法**: <具体文件/模块 / 参考思路>
- **备注**: <坑 / 注意点 / 参考原因>
```

### 项目列表

#### soullink-emotion-sdk

- **仓库**: [nanlingyin/soullink-emotion-sdk](https://github.com/nanlingyin/soullink-emotion-sdk)
- **许可**: MIT ✓(可直接采用/改造)
- **版本/commit**: 2026-07-18 创建,最近 push 2026-07-29(很新);当前 80 stars / 7 forks / 0 open issues
- **用途**: 心情/情绪 SDK — 升级 vbcpet 现在比较单一的 mood 系统(单数值+参数表)
- **在本项目的用法**: 取**框架无关的 `engine`**(纯 TypeScript,零依赖)跑在浏览器/Node,把输出的 `snapshot.live2dParams` 喂给 pixi-live2d-display 的 `model.setParameterValue()`
- **备注**:
  - **核心理念**: 连续 VAD 情绪空间(Valence-Arousal-Dominance 三维)+ FACS/AU 表情合成 + MotionMixer 分层动画 + ModelProfile 参数映射 —— 比我们"一个 0-100 数值"细腻得多
  - **内置 1400 条中文语料规则分类器**(runtime-core `sendMessage`),无 LLM 也能即时情绪反应
  - ⚠️ 兼容性:其 `live2d-pixi` 渲染包依赖 **PIXI v7**,我们用的是 v8 + @jannchie fork → **只取纯 TS 引擎层**,不碰渲染包
  - 依赖: Node 20.19+ ✓(本机 v24)
  - 授权范围: Live2D 模型与 Cubism Core 不在 SDK 许可范围内(需各自遵循 Live2D 条款)
  - **✅ 已验证 (2026-08-12)**:
    - profile-generator 自动为 Hiyori 生成 22 参数映射 (`live2d/Hiyori/soullink.profile.json`);engine 纯 Node 跑通(`live2d/demo_soullink.mjs`)
    - **消息自动分类**: `MessageReactionClassifier.classify()` 中文实测 ——"这破服务器又崩了"→confused、"好喜欢你呀"→shy/bashful、"我有点困了"→tired/drained ✓
    - **已接入 render.html** (importmap 加载 engine dist,零构建):engine 每帧 `update()` → `setParameterValue` 写参数;`applyEmotion/setMood/setExpression` 兼容层保留 PC 端契约,新增 `sendMessage(text)` 本地分类;engine 不可用时自动回退旧 mood 参数表
  - **待实测**: 浏览器画面效果(旧 idle motion 与 engine 参数可能互相覆盖,需调 motionStyle.gain / 暂停旧队列)

---

## 三、生态候选(可参考,按需选用)

> 与 Live2D / 桌面宠物 / ComfyUI 换皮相关的知名项目,供挑选。如果它们里有你想要的,直接挪到第二节并补全字段。

| 项目 | 仓库 | 适合场景 |
|------|------|---------|
| guansss/pixi-live2d-display(原版) | [guansss/pixi-live2d-display](https://github.com/guansss/pixi-live2d-display) | PixiJS v6/v7 的老项目兼容 |
| Panzer-Jack/pixi-live2d-display | [Panzer-Jack/pixi-live2d-display](https://github.com/Panzer-Jack/pixi-live2d-display) | 另一个维护分支 |
| pixi-live2d-display-webgal | [WebGAL](https://github.com/WebGAL) | PixiJS v6 + WebGAL 引擎场景 |
| live2d-widget | [stevenjoezhang/live2d-widget](https://github.com/stevenjoezhang/live2d-widget) | 博客看板娘(轻量,可参考加载/布局思路) |
| ComfyUI 官方节点库 | [Comfy-Org/ComfyUI](https://github.com/Comfy-Org/ComfyUI) | 换皮工作流的 API 调用基础(已在用) |

---

## 维护约定

- 本项目所有参考项目以**本文件为准**,不散落在对话里
- 采用新项目前确认**许可**能覆盖我们的用途(桌面宠物硬件+PC 推流,大概率个人使用)
- 每次有大的版本升级,回来更新"版本/commit"
