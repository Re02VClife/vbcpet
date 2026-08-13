# Live2D 情绪系统 (soullink-emotion 集成) — 现状存档

> 状态: **已打通链路,视觉幅度待优化** (2026-08-13 存档,暂时搁置)
> 参考库: [nanlingyin/soullink-emotion-sdk](https://github.com/nanlingyin/soullink-emotion-sdk)(MIT)

## 已实现的

`live2d/render.html` 集成了 soullink-engine(纯 TS,零依赖):

| 能力 | 状态 | 说明 |
|------|------|------|
| ModelProfile 生成 | ✅ | `tools` 生成,`live2d/Hiyori/soullink.profile.json`(21 参数 FACS→Cubism 映射) |
| 消息→情绪分类 | ✅ | `MessageReactionClassifier` 内置 1400 中文语料。实测: "我喜欢你"→shy/bashful、"服务器崩了"→confused |
| VAD 情绪驱动 | ✅ | `SoullinkRuntime.triggerIntent()` + `applyVADTarget()` 持续 6s |
| 参数增强层 | ✅ | 帧循环里 shy→低头16-26°/歪头/眼神/脸红; happy→微笑 |
| OSD | ✅ | DOM 覆盖层(右上角)显示 emotion/variant/mood,不依赖 PIXI.Text |
| 诊断 | ✅ | `runDemo()` 画面内面板(底部)、`eyeTest()` 闭眼测试 |
| 兼容层 | ✅ | `setMood/applyEmotion/playMotion/getStatus` 接口保留,PC 端契约不变 |

## 已验证的事实(不要推翻)

1. **参数链路完全通**:engine 输出 → `setParameterValue` → coreModel 读回一致(demo STEP3 实测 -26 = -26)。
2. **engine 表情本身幅度很小**:shy 低头峰值约 -0.92°(node),FACS headY≈-0.03。**必须靠增强层放大才可见**。
3. **版本组合只能用 `pixi-live2d-display@1.3.0` + `pixi.js@8.19.0`**:
   - `1.4.0 + 8.19`:WebGL 错误累积,运行后模型变绿色色块
   - `1.4.0 + 8.5`:直接崩溃(`Cannot use 'in' operator`)
   - `1.3.0 + 8.19`:有 WebGL 错误但模型稳定(接入前就有,原注释"可接受")

## 已知问题(未解决,搁置原因)

1. **WebGL buffer 冲突**(`bindBuffer: element array buffers can not be bound to a different target` + `glDrawElements`):
   根因 = PIXI v8 的 buffer 复用 vs Cubism 的 GL 状态保存/恢复。
   试过 patch Cubism restore(解绑 null)→ 引起 glDrawElements 失败,已回滚。
   未试:降级 pixi.js 到 8.0-8.4 时代(8.5 崩是因为 1.4.0 依赖新 API)。
2. **情绪视觉幅度不足**:shy 增强到 -26° 低头 + 歪头 + 脸红,用户仍觉得不明显。可能与 WebGL 渲染损坏有关(错误累积后 mesh 重绘失败)。
3. 待办:表情幅度/时长调优、WebGL 兼容根治、接串口链路。

## 恢复/调优入口

- 浏览器控制台: `runDemo()`, `eyeTest()`, `sendMessage("我喜欢你")`, `getEmotionState()`
- 增强层: `render.html` 帧循环里 `情绪表情增强` 注释段(clamp/倍数/至少值)
- OSD/面板: 页面顶部 HTML div(#osd / #demoPanel),与 PIXI.Text 无关

## 下一步候选(被搁置时)

1. 接 PC 串口链路: Claude 回复 → `sendMessage` → 情绪反应(端到端打通)
2. 换皮优先(Hiyori→自绘形象),情绪系统等形象定型再调
3. WebGL 兼容根治: 试 pixi.js 8.0-8.4 或换 guansss 原版(v7)
