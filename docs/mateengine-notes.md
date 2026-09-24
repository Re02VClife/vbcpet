# MateEngine 参考笔记（X3.3.0 源码深挖）

> 本机两份参考（用户提供）：
> - **已编译程序**：`E:\MateEngine\MateEngineX.exe`（Unity StandaloneWindows64 + Mono，Steam 集成）
> - **Unity 源码**：`E:\Mate-Engine-Public-Release-X3.3.0`（`Assets/MATE ENGINE - *` 全量）
> - 开源项目：[shinyflvre/Mate-Engine](https://github.com/shinyflvre/Mate-Engine)，AGPL v3 + MateProv2 混合许可；默认角色版权归 [Yorshka Shop](https://yorshkasencho.booth.pm/)，**勿在自建产物里再分发**
>
> （旧笔记只看了 1.9.0b 打包产物；本文件对齐到 X3.3.0 **源码**，能读到完整实现。）

## 一、它是什么

Unity 打造的**开源 VRM 桌面虚拟伴侣**（Desktop Mate 替代品）：VRM 角色 + 本地 LLM（Qwen GGUF）+ 透明桌面常驻窗口 + 模组/DLC 系统。核心卖点：自定义 VRM、可编辑 blendshape、事件驱动行为（重力/行走/睡眠/喂食/随音乐起舞）、AI 聊天。

对 vbcpet 的价值是**架构 + 行为参考**，不是直接搬运（它是 Windows Unity 应用，和我们的 Electron 桌面壳 / P4 硬件是不同层）。

## 二、资源清单（模型可搬性）

| 角色 | 位置 | 可搬？ | 说明 |
|---|---|---|---|
| **Zome**（ゾメちゃん） | `MATE ENGINE - Avatar/Zome.vrm` | ✅ VRM 0.x, 26MB | 默认角色，作者 Yorshka，v1.3，24 组表情 |
| aldina | `MATE ENGINE - Avatar/DLCs/aldina.vrm` | ✅ VRM 0.x, 12MB | DLC，17 组表情 |
| Lazuli | `MATE ENGINE - Avatar/DLCs/Lazuli_VRM.vrm` | ✅ VRM 0.x, 58MB | DLC，MToon 材质 |
| Ayrina（ME_02 DLC） | `MATE ENGINE - DLCs/ME_02/Ayrina/*.prefab` | ❌ 无 .vrm | Unity 烘焙资产（`*.baked.asset` + prefab），只能给 Unity 用 |

> 三个 VRM 曾拷入 `live2d/models/`，**现已移出仓库**（体积大 + 原作者要求勿再分发，见 [`../live2d/models/README.md`](../live2d/models/README.md)），需要时从 MateEngine 发布包自行放回。DLC 目录里每个角色的 `.BlendShapes/*.asset`、`.Materials/*`、`.Meshes/*.baked` 都是 Unity 序列化 YAML（`VRM::VRM.BlendShapeClip`），记录 `BlendShapeName + Preset + Values[RelativePath/Index/Weight]`，是**离线读懂每个角色表情如何绑定到 morph index** 的现成依据。

## 三、表情/情绪管线（核心参考）

### 1. `UniversalBlendshapes.cs` —— 统一表情层（最值得抄）

一个「17 键规范表情集」同时驱动 VRM 0.x 和 VRM 1.0：

```
规范键: Blink / Blink_L / Blink_R / LookUp / LookDown / LookLeft / LookRight
       / Neutral / A / I / U / E / O / Joy / Angry / Sorrow / Fun
```

- VRM 0.x：映射到 `BlendShapePreset` 枚举（Blink/Joy/Angry/Sorrow/Fun/...）。
- VRM 1.0：按名映射（**关键约定，可直接抄**）：

| 规范键 | VRM 1.0 表达式名 |
|---|---|
| Joy | `happy` |
| Angry | `angry` |
| Sorrow | `sad` |
| Fun | `relaxed` |
| A / I / U / E / O | `aa` / `ih` / `ou` / `ee` / `oh` |
| Blink / Blink_L / Blink_R | `blink` / `blinkLeft` / `blinkRight` |
| LookUp/Down/Left/Right | `lookUp` / `lookDown` / `lookLeft` / `lookRight` |

- **fade/hold 回落状态机**（`UpdateState`）：每个表情值在「保持 minHoldTime → 以 fadeSpeed 回落到 0 → 超过 safeTimeout 硬归零」之间过渡。这正是「表情自然放松」的实现，我们已移植到 `render_vrm.html`。

### 2. `BlendshapeManager.cs` —— 运行时按名扫描

每 0.75s 扫当前角色所有 `SkinnedMeshRenderer` 的 blendShape 名 → 生成 UI 滑块 → 值 JSON 持久化到 `persistentDataPath/Blendshapes/<角色>_Blendshapes.json`。对应我们思路：渲染页在运行时从模型自取 morph 名（`render_vrm.html` 的 `_setupVRM0`/`_collectMorphs` 同款）。

### 3. `AvatarAnimatorController.cs` —— Animator 参数约定

统一用 Animator 参数表达状态（对所有注入的角色共用同一套 controller）：
`isIdle / isDragging / isDancing / isTalking / IsSleeping / isMale / IdleIndex / DanceIndex`。
- idle 每 12s 在 10 个 idle 动画间平滑切换（3s 过渡）。
- 随音乐起舞：NAudio 枚举系统音频会话，检测「允许应用列表」（Spotify/Firefox 等）音量 > 阈值 → 触发 `isDancing`。

## 四、行为特性清单（可拓展点）

| 特性 | 源码 | 做法 | 对我们可移植性 |
|---|---|---|---|
| **重力/拖拽惯性** | `AvatarGravityController.cs` | 读窗口位置 delta → 归一化 impact × 系数 → 注入 SpringBone ExternalForce（VRM0）/ gravityDir+power（VRM1） | ★★★ 极廉价：Electron 主进程报窗口位移 → 渲染页注入 three-vrm springbone |
| **随机闲话气泡** | `AvatarRandomMessages.cs` | 双池：idle 池（随机 10~60s）+ onActive 池（状态触发）；打字机流式 + despawnTime 自动消失 + `isTalking` 标记 | ★★★ 高价值：对应我们 `sendMessage` + 加气泡 UI |
| **睡眠** | `AvatarSleepController.cs` | 闲置计时（30~360s）→ `IsSleeping`；拖拽等 wakeUpBool 唤醒；白名单状态内才累加 | ★★ 中：映射到 `mood` 回落即可（PNG 大肥鱼已有 sleep 状态帧） |
| **随机行走** | `AvatarLocomotionController.cs` | 随机方向 `SetWindowPos` 移动窗口 + 屏幕边缘/角色边界阻挡 | ★★ 中：Electron 可移动窗口；需 walk 动画 |
| **喂食** | `AvatarFoodController.cs` | 鼠标跟随食物 + 头部接触判定 + 弹簧摆动（`Spring()` 阻尼） | ★ 低（依赖食物模型 + 音频） |
| **头/脊柱/视线追踪** | `AvatarMouseTracking.cs` | 头/脊柱按鼠标偏航俯仰 Slerp，视线走 VRM lookAt | ★★★ three-vrm `lookAt.target` 已覆盖视线；头/脊柱需骨骼操纵 |
| **AI 系统提示词** | `AISystemPromptBinder.cs` | 提示词写文件 `LocalLow/Shinymoon/MateEngineX/ZomeAI_prompt.txt` → `LLMCharacter.SetPrompt` | ★★ 中：我们的 `agent_bridge` 可读「每角色提示词文件」 |
| **本地 LLM** | `StreamingAssets/LLMManager.json` | Qwen3-4B Q4_K_M GGUF，llama.cpp（`fullLlamaLib:true`） | ★★ 中：可作断网回退（PC-Host 侧） |

## 五、已移植到 vbcpet（本次）

1. **VRM 默认模型 = Zome**：`render_vrm.html`、`desktop_pet_display.py`、`stream_simple.py` 的 vrm 默认从错误/占位值改为 `models/Zome.vrm`。
2. **统一表情层（`render_vrm.html`，参考 UniversalBlendshapes）**：
   - 情绪名大小写不敏感路由（VRM1 expressionManager / VRM0 blendShapeGroups / 纯 glb morphs 三路径统一）。
   - 加入 Zome 自定义表情 `wink/niyari/star/heart/fun2/sorrow2/sorrow3` 与 VRM1 补充 `relaxed/drowsy`。
   - **表情回落**：保持 hold → 线性 fade 回 neutral（fade/hold 状态机移植）。
   - **回退路径自动眨眼**（无 three-vrm 时定时驱动 `blink`）。
3. **`tools/extract_vrm_meta.mjs`**：解析 .vrm（GLB）→ 打印 meta + blendShapeMaster 表情组，用于核对任意模型表情清单。

## 六、判断

- Mate Engine **不是能直接搬到 P4 的**：它是 Unity 桌面应用，跑 Windows。我们的落地仍是「Electron 桌面壳 + P4 串口推流」双宿主。
- 真正值得继续抄的三件事：① **UniversalBlendshapes 的统一表情约定**（已移植到 VRM 管线）；② **事件驱动行为**（重力/闲话/睡眠——见上表优先级）；③ **每角色提示词文件 + 本地 Qwen GGUF 断网回退**（offline-first 理念）。
- 下一步建议（按性价比）：随机闲话气泡 → 窗口拖拽惯性（重力）→ 睡眠/闲置回落 → 本地 LLM 回退。
