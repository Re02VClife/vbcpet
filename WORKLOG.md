# vbcpet 工作日志

## 2026-07-21 (周一)

### 代码整理与提交
- 将固件从单片 `vbcpet_main.c` 拆分为 7 个模块：transport, channel_demux, display_pipeline, interaction_engine, text_render, text_input
- 编译通过，0 error 0 warning
- 3 个 commit：firmware 重构、文档更新、PC 端推流+测试资源整理

### 集成验证
- 新模块固件烧录到 WT9932P4-TINY 开发板
- 全部 9 项集成测试通过：NVS、启动画面、engine hello、heartbeat、USB transport、demux 分流、display_pipeline 解码刷屏、端到端推流
- 7 个新模块，0 bug

## 2026-07-22 (周二)

### 硬件方案决策
- 决定使用鱼鹰 ESP32-P4C6-Core 核心板（替代 WT9932P4-TINY）
- 确认 C6 WiFi 通过 SDIO 连接 P4，吞吐 10x 余量（需求 5Mbps vs 实际 50Mbps）
- 音频方案：MAX98357A + INMP441，I2S 接口
- 显示兼容方案：SPI + MIPI DSI 双后端，Kconfig 编译时选择

### 显示后端抽象
- 拆出 display_backend.h 内部接口
- display_pipeline_spi.c：ST7789V 后端
- display_pipeline_dsi.c：MIPI DSI 占位
- Kconfig.projbuild：编译时选择，sdkconfig.defaults 默认 SPI

### Claude 交互桥 v0.1
- serial_bridge.py：Claude SDK ↔ COM16 ↔ P4 固件
- 实现文字交互闭环：PC 键盘输入 → Claude → 回复显示到 P4 屏幕
- 支持 /y /n /a 按键授权

### 中文字体支持
- tools/gen_font.py：从 SimSun 生成 16x16 位图 C 头文件
- font_cjk.h：7548 个字形，241KB
- text_render.c 新增 UTF-8 解码 + CJK 字形查找 + 渲染
- ASCII 字体保留内嵌

### PC 端提交
- serial_bridge.py、gen_font.py、中文字体相关提交

## 2026-07-23 (周三)

### serial_bridge.py v0.2
- 合并 Live2D JPEG 推流（原 stream_simple.py 功能）
- Claude 状态 → Live2D 表情动作映射（thinking/done/blocked）
- 数字按键 [1][2][3][4] 模拟 yes/no/yes-always/panel
- CTRL 消息通过 asyncio.Queue 串行化，避免并发写冲突
- on_status 从 async 修复为同步回调

### 显示稳定性排查（核心难点）
- **问题1**：统一 flush 的 g_buf_idx 初始值 bug → 第一帧前 flush 垃圾数据 → 花屏
  - 修复：feed_jpeg 内部 flush + text_render 独立 flush
- **问题2**：USB Serial JTAG TX/RX 冲突 → 心跳发送阻塞主循环
  - 修复：心跳改为非阻塞 send_ctrl_nb(timeout=0) + 加大 TX 缓冲
  - 最终方案：完全禁用 P4→PC TX，PC 端靠推流活动判断存活
- **问题3**：SPI DMA 队列溢出 → 每循环都 flush（1000次/秒）填满队列
  - 修复：只在有新帧时 flush（g_frame_decoded 标志）
- **问题4**（根因）：JPEG flush 的 SPI DMA 未完成时，text_render_box 写同一块内存 → DMA 读到半新半旧数据 → 花屏崩溃
  - 修复：改为单缓冲，先解码→叠加文字→一次 flush
- **问题5**：g_text_dirty 标志导致文字只渲染一帧 → 下一帧 JPEG 覆盖消失
  - 修复：去掉 dirty 标志，每帧都叠加文字
- **问题6**：draw_ascii/draw_cjk 把背景像素也画成黑色 → 整个字符变成黑色方块
  - 修复：改为透明背景，只画前景像素

### 当前状态
- Live2D 推流 + 文字交互实现（bridge v0.2）
- 文字渲染通路打通的最后一步修复中
- 鱼鹰核心板待购入后开始原理图设计

## 2026-07-24 (周四)

### 文字渲染通路打通
- **问题7**：text_render_box 只在 text_dirty=true 时渲染，第二帧 JPEG 覆盖 FB 后文字消失
  - 修复：去掉 text_dirty，每帧都重新叠加文字
- **问题8**：draw_ascii/draw_cjk 把背景像素也画成前景色 → 整个字符变成黑色方块而非字母
  - 修复：改为透明背景，`if (!glyph_bit) continue;` 只画前景像素
  - 白色矩形/全屏涂白/单字符 'A' 三级逐层诊断法确认：FB 写入通路 OK → 坐标 OK → 字符渲染 OK

### 串口协议稳定性
- **问题9**：行缓冲 256 字节限制 → 长消息（Claude 讲故事、天气报告）JSON 被截断，cJSON 解析失败
  - 修复：行缓冲 256→512→1024 字节
- **问题10**：串口写超时时 `\n` 可能丢失 → demux 永远等不到行尾，后续所有数据堆在同一行 → 状态崩溃
  - 修复：channel_demux 加 500ms 行超时自动重置保护
- **问题11**：PC 端截断 300 字符方案 + 100ms CTRL 延迟 + 停用 work_status/panel 下行消息，减少串口中断频率

### 滚动显示尝试
- 新增 text_render_count_lines() API 计算换行总行数
- 实现从底部开始显示（g_scroll_line = total_lines - CHAT_H）+ 2.5s 自动向上滚动 + 到底循环
- 遇到稳定性问题，暂时回退到最简显示前 3 行

### 当前状态
- ✅ Live2D 推流稳定
- ✅ 中文文字渲染正确（透明背景，显示在屏幕底部 3 行区域）
- ⚠️ 长消息/多次对话后偶发冻结，与 CTRL 消息批次数和时序相关
- ⬜ 滚动显示待稳定后重新实现

## 2026-07-27 (周日)

### Hiyori 模型修复 — 动作 & 表情

- **Motion 文件替换**：从 Live2D 官方 GitHub (CubismWebSamples) 下载 10 个真实 motion 文件 (8-19KB)，替换本地 214 字节 stub
  - 使用 jsdelivr CDN 绕过 raw.githubusercontent.com 网络问题
  - 验证为 Cubism 3 格式，31 条参数曲线，4.7s/30fps
- **表情系统**：Hiyori 官方无表达式文件（.exp3.json 为空），自己实现参数驱动表情
  - 调查发现 `@jannchie/pixi-live2d-display` 参数 API：`model.setParameterValue()` / `model.getParameterValue()`
  - **内部 API 陷阱**：`internalModel.parameterSetValue` 不存在，`internalModel.setParameterValueById` 也不存在 — 正确 API 在 top-level model 对象上
  - 实现 `EMOTION_PRESETS` + `applyEmotion()` 带 easeInOutQuad 500ms 过渡
- **创建 `tune_expressions.html` 可视化调参工具**：
  - 13 个面部参数滑块 + 5 个预设按钮 (Neutral/Happy/Sad/Surprised/Angry)
  - 解决 MIME type (.mjs→text/plain) → 自定义 HTTP server `extensions_map`
  - 解决 `window.PIXI` 未暴露 → `No Ticker` 警告
  - **核心难点 — 参数被 idle 动画覆盖**：
    - 尝试 `setParameterValue` → 被每帧 motion 覆盖
    - 尝试 `model.update() + app.render()` → 无效
    - 尝试 `updateNaturalMovements = no-op` → 只禁用物理，不影响 motion
    - 尝试 `motionManager.stopAllMotions()` → 独立 eyeBlink 子系统仍在跑
    - **最终方案**：`motionManager.update = no-op` + `setEyeBlinkEnabled(false)` → 参数全由用户控制
  - 用户用调参工具微调了 5 个表情的预设值（如 happy=闭眼笑 ^_^, angry=瞪眼咧嘴）

### 心情值系统 (Mood)

- **render.html**：右上角 `Mood:60` 显示，`window.setMood(v)` + `getMood()`
  - 0-25=angry, 25-45=sad, 45-65=neutral, 65-80=happy, 80+=surprised
- **serial_bridge.py**：心情引擎
  - Claude 状态偏移：done=+8, blocked=+5, editing=+3, running=0, thinking=-2, **error=-15**
  - 用户行为偏移：发消息=+3, 允许工具=+5, 拒绝工具=-3
  - 闲置回归：每 10 秒向基线 60 漂移 1 点
  - 命令行直接控制：`mood+20` / `mood-30`
  - Stream 输出：`[stream] 180 frames, 13.8 fps, 12KB/frame, mood:65`
- **表情生效的关键修复**：`applyEmotion()` 先 `stopAllMotions()` → 过渡参数 → 5 秒后恢复 idle

### 固件编译 & 稳定性

- **ESP-IDF 编译问题**：Git Bash (MSys) 中 idf.py 检测到 `MSYSTEM` 环境变量后只打警告不执行 main()
  - 修复：批处理中 `set "MSYSTEM="` 绕过检查
- 编译成功：8 个固件文件 + scroll display 逻辑，0 error 0 warning
- 烧录成功：ESP32-P4, COM16, 673KB
- **纯推流压测** (stream_test.py)：210 帧稳定，**13.8fps**, 12-14KB/帧

### Git 提交 (8 commits)

| Commit | 内容 |
|--------|------|
| `4bab522` | Hiyori motion stubs → 真实文件 + tune_expressions.html |
| `0f4c457` | firmware: 单缓冲 + 行超时 + 透明文字 + 滚动显示 |
| `0e81fef` | pc-host: serial_bridge v0.2 (推流+Claude+表情) + stream_test |
| `e60de58` | tune_expressions.html 调参修复 (禁用所有自动动画) |
| `7d2ab8b` | applyEmotion: im.parameterSetValue → model.setParameterValue |
| `38fc2d2` | 心情系统 (Mood 0-100, 右上角显示, 自动漂移) |
| `c79180c` | mood+/- 命令行测试指令 |
| `0947fda` | applyEmotion 先停 idle motion 再设表情 (关键修复) |

### 当前状态

- ✅ Hiyori 动作正常（真实动画 8-19KB motion 文件）
- ✅ 表情可控（参数驱动，5 个预设，用户已调参）
- ✅ 心情系统完整（Claude 事件 → mood 偏移 → 表情映射 + 右上角显示）
- ✅ 固件稳定（单缓冲 + 行超时 + 滚动显示，13.8fps 推流）
- ✅ 调参工具可用（`live2d/tune_expressions.html`）
- ⬜ 需 P4 硬件连接验证：serial_bridge.py 端到端 + 心情表情联动

## 2026-07-28 (周一)

### 心情 → 表情持续影响系统 (动作队列 v3)

**背景**：旧方案 `applyEmotion()` 停止 idle → 过渡表情 → 5 秒后恢复 idle → **表情被动画覆盖消失**。需要心情持续影响角色面部。

**尝试过的方案**：

1. **motionManager.update() hook** — 每帧动画后混合心情参数
   - 复用 `motionManager.update` 在动画更新后叠加表情参数
   - 问题：PIXI v8.19 + `@jannchie/pixi-live2d-display` 1.3.0 + Cubism Core 5.1.0 存在底层 WebGL 缓冲冲突
   - `bindBuffer: element array buffers can not be bound to a different target` × 1635 错误
   - 根因：Cubism SDK 的 `doUpdateMotion` → `updateParameters` 与 PIXI v8 的 WebGL 状态管理冲突
   - `tune_expressions.html` 正常是因为禁用了所有动画；有动画持续跑就触发

2. **脉冲动画方案** — 80% 时间静态表情 + 20% 动画脉冲
   - 禁用持续 idle，用 `setTimeout` 周期性短暂播放 2.5s 动画
   - 问题：需要禁用原生眼眨+自然运动（否则仍触发 WebGL 错误），需手动眨眼
   - 用户要求保留原生系统

3. **动作队列方案 (最终)**：
   - 保留原生眼眨 + 自然运动（WebGL 错误可接受，仅在动画播放期出现）
   - 拦截 `startRandomMotion`，手动控制队列节奏
   - 循环：播放 idle → 轮询 `isFinished()` → idle 结束 → 应用心情表情 2.5s → 下一 idle
   - 表情参数直接 `setParameterValue` 控制（无 motion 覆盖）
   - 平滑过渡 `_transitionMoodParams`：easeInOutQuad, 300-620ms

**render.html 最终架构**：
```
startRandomMotion 拦截 → _motionQueueLoop()
  ┌──────────┐     ┌──────────────┐     ┌──────────┐
  │ Idle ~4.7s│ → │ 表情 2.5s    │ → │ Idle ~4.7s│ → ...
  │ (原生眨眼) │    │ (mood 表达式) │    │ (原生眨眼) │
  └──────────┘     └──────────────┘     └──────────┘
```

**serial_bridge.py 调整**：
- 移除 `_apply_l2d_status` 中冗余的 `_l2d_motion("Idle")` 调用
- 心情引擎、Claude 状态偏移、mood± 指令保持不变

### 抗锯齿尝试

用户反馈 Live2D 锯齿严重：
- MSAA (`antialias: true`) → 无效
- SSAA (`resolution: 2`, 480×640 内部渲染) → 无效
- 根因：模型在 240×320 画布上 `scale: 0.16` 仅渲染 ~76×107 像素，细节不足 2 像素宽
- 结论：抗锯齿技术无法解决超小渲染的锯齿，需调大模型或重构图，暂搁置

### 硬件 BOM 全部定稿

BOM 原本 5 个 ⚠️ 待确认项全部解决：

| 项目 | 决定 |
|------|------|
| 显示屏 | **YDP430BT009-V1** (鱼鹰光电), 4.3" IPS, 480×800, ST7102 MIPI-DSI 2-lane + 触摸 |
| PA 功放 | **NS4150**, GPIO 控制, 3W D 类 |
| 在位检测 | **PN532 NFC** (I²C), 手机放底座自动识别 + 触发配对 |
| PD 供电 | **CH224K** 请求 9V/2A, 5V 回退; 板载 3.3V Buck + 1.8V LDO |
| 主控 | **ESP32-P4C6-Core** (鱼鹰光电), 88pin 邮票孔, P4+C6 一体模组 |

### 硬件文档产出

- **[hardware/final-pinmap.md](hardware/final-pinmap.md)** — 最终板完整引脚分配
  - 基于 P4C6-Core 88pin 逐个信号落实到 GPIO
  - DSI / I²C / I²S / 触摸 / 按键 / NFC / LED / USB / 调试串口
  - 供电架构：USB-C 9V PD → 3.3V Buck + 1.8V LDO + 背光升压
  - I²C 地址表：ES8311=0x18, ES7210=0x40, PN532=0x24
- **[hardware/devboard-pinmap.md](hardware/devboard-pinmap.md)** — 当前开发板 WT9932P4-TINY 实际引脚占用与兼容性分析
- 解析了 2 份 PDF 数据手册：YDP430BT009-V1 (屏幕) + ESP32-P4C6-Core (模组)

### 关键发现

- **GPIO14-19 在 P4C6-Core 上未引出**（内部 PSRAM/Flash 占用）
  - 当前固件的 IO16(BCLK)、IO18(SCL)、IO15(DOUT) **不可用**
  - 最终板改为：SCL→IO21, BCLK→IO22, DOUT→IO23
- **ST7102 驱动有 ESP-IDF 官方组件** (`espressif/esp_lcd_st7102`)，兼容性好
- **触摸 I²C 为 1.8V**，需 TXS0102 电平转换，直接接 3.3V 会烧 IC
- **ESP32-P4C6-Core 模组和 YDP430BT009-V1 屏幕同厂**（均为鱼鹰光电），供应协同有优势

### 当前状态

- ✅ 心情持续影响表情（动作队列 v3，原生眼眨+自然运动保留）
- ✅ BOM 全部定稿，5/5 ⚠️ 已解决
- ✅ 最终板完整引脚分配
- ✅ 调参工具可用（`live2d/tune_expressions.html`）
- ⬜ 固件引脚适配（IO15/16/18 → IO21/22/23）
- ⬜ 开始绘制原理图 (KiCad)
- ⬜ P4 硬件端到端验证

## 2026-07-29 (周二)

### 原理图设计文档

- 编写 [schematic-design.md](hardware/schematic/schematic-design.md) — 5 页完整设计，逐元件逐脚连接表
- 生成 [bom-lcsc.csv](hardware/schematic/bom-lcsc.csv) — 立创商城 BOM 含 C 编号，可直接导入嘉立创 EDA
- 更新 [schematic/README.md](hardware/schematic/README.md) — 嘉立创 EDA 专业版绘制指南

### 设计修正（用户审查 + 自主发现）

**供电架构修正**：

| # | 问题 | 原来(错) | 修正 |
|---|------|----------|------|
| 1 | P4C6 封装 | 2×44 双排 | **22×4 四面引脚** (邮票孔核心板) |
| 2 | 背光电压 | 26V (推算) | **12.2V/40mA** (从 PDF 第 8 页提取) |
| 3 | NS4150 供电 | VBUS 9V (超规格) | **+3V3** (最高 5.5V) |
| 4 | CH224K CFG | GND (实际=15V) | **+3V3** (请求 9V) |
| 5 | Buck 芯片 | SY8088 (最高 5.5V) | **MP2315** (4.5-24V 宽压) |
| 6 | MP3302 供电 | VBUS 9V (超规格) | **+3V3** (最高 6V) |

**CH224K 讨论**：WT9932P4-TINY 开发板用最简单 USB 5V→3.3V 稳压，没用任何 PD 芯片。总功耗 ~5W，USB-C 5V/3A=15W 绰绰有余。CH224K 暂时保留但可去掉。

**USB 通信**：确认 P4 内置 USB Serial JTAG + DFU，单根 USB-C 线搞定供电+串口+烧录+JTAG 调试，与开发板一致。无需 CP2102/CH340/FT232 等桥接芯片。

### 音频架构重设计

用户指出小智项目用 MAX98357A + INMP441 全数字方案 VS 我设计的 ES7210+ES8311+NS4150。

**旧方案 (废弃)**：
```
ES7210(ADC) + 模拟 MEMS → I2S TDM → P4
P4 → I2S → ES8311(DAC) → NS4150(PA) → 喇叭
```
- 3 颗 IC + 2 颗 I2C 地址 + 模拟走线 + AGND 分区 + 磁珠

**新方案 (全数字 I2S)**：
```
P4 I2S0 ↔ MAX98357A (I2S DAC+3W PA) + ZTS6672×2 (I2S 数字麦)
```
- **MAX98357A** (CH98357 国产替代, ¥6)：I2S 直入, 内置 D 类 3W, 滤波免输出
- **ZTS6672 ×2** (ICS-43434 平替, ¥3/颗)：I2S 数字 MEMS 麦, 灵敏度-26dB, SNR 65dB
- 删 3 颗 IC (ES7210/ES8311/NS4150)
- 删 6 颗阻容 (隔直/滤波)
- 无模拟走线, 无 AGND 分区, 无磁珠
- I2C 总线上只剩 NT3H1101 + 触摸 (原 4 设备)
- BOM 净减少 8 行

**ZTS6672 引脚 (从 EDA 库验证)**：7 脚, 无 MCLK — {LRC, SELECT, GND, BCLK, VDD, DATA, CONFIG}。文档中 8 脚带 SCK/MCLK 的描述**全部错误**（用户画图时发现）。

**不需要 MCLK**：ZTS6672 和 MAX98357A 都支持从 BCLK 内部生成时钟。GPIO5 释放。

### BOM 优化

| 改动 | 旧 | 新 | 理由 |
|------|----|----|------|
| NFC | PN532 (¥15+) | **NT3H1101** (¥2, I2C 0x55) | 只需手机触碰开 APP，数据走 C6 WiFi |
| 保险丝 | PTC 1206 2A | **0Ω 跳线 + TVS** | 开发板同方案，简单可靠 |
| 音频 | ES7210+ES8311+NS4150 | **MAX98357A+ZTS6672×2** | 全数字 I2S，少 3 颗 IC |

### 用户开始绘制原理图

- 在嘉立创 EDA 专业版中建 P4C6-Core 模组符号 (M1), 已接好大部分引脚
- 放置了 CH224K, MP2315, CH98357, ZTS6672, FPC30, 按键等
- **发现 ZTS6672 引脚与文档不符** (7 脚 vs 8 脚, 无 MCLK)
- **CH98357 接线需修正**：DIN→I2S_DOUT (非 IO7), LRCLK→I2S_WS (非 IO16), BCLK→I2S_BCLK (非 IO15)

### 当前状态

- ✅ 心情持续影响表情（动作队列 v3）
- ✅ BOM 全部定稿 + 多轮修正
- ✅ 最终板完整引脚分配
- ✅ 原理图设计文档 (5 页)
- ✅ BOM CSV (31 行)
- ✅ 音频架构重设计 (全数字 I2S)
- ✅ 模块化设计方案
- 🔧 原理图绘制中 (嘉立创 EDA)
- ⬜ 固件引脚适配

## 2026-07-30 (周三)

### 原理图绘制 + 设计迭代

用户在嘉立创 EDA 中画原理图，过程中发现多处理论与实物不符，逐项修正。

### 关键修正

**1. ZTS6672 引脚错误**
- 文档写成 8 脚带 SCK/MCLK，实际 ZTS6672 是 **7 脚，无 MCLK**（用户从 EDA 库中确认）
- ZTS6672 和 MAX98357A 都能从 BCLK 内部生成时钟，GPIO5(MCLK) 释放

**2. 触摸 IO 是 3.3V，不是 1.8V**
- PDF 解析偏差导致误判。用户按官方文档建 FPC 符号，TP_VDD 标 3.3V
- 删掉整个 1.8V 供电路径：ME6211C18、TXS0102、TXS0101 ×2、C7/C8
- 触摸 4 个引脚直接 3.3V 连 P4 GPIO

**3. 1.8V 不存在 → 全部文档重写**
- 6 个文档更新：bom.md、final-pinmap.md、power-tree.md、schematic-design.md、bom-lcsc.csv、README.md
- final-pinmap.md 完全重写，schematic-design.md 完全重写

**4. 音频架构迭代**
- 原设计 ES7210+ES8311+NS4150 → 改为 MAX98357A+ZTS6672×2 全数字 I2S
- 最终：MAX98357A 外购模块 + ZTS6672 外购模块 ×2
- 无 MCLK 需求，I2S 总线只需 BCLK/WS/DOUT/DIN

**5. 其他选型修正**
- PN532 → NT3H1101 → **NT3H2111** (NXP 升级版, 更便宜, pin2pin)
- 自恢复保险丝 → **0Ω 1206 + TVS**
- CH224K PD → **去掉** (USB-C 5V/15W 远超总功耗 4.2W)
- 轻触开关 → **Cherry MX 轴体** ×4
- WS2812B 3535 ×1 → **5050 ×4** 键下级联 RGB
- 背光 MP3302 → **MT3608** (更常见好买)
- Buck SY8088 → **MP2315** (宽压 4.5-24V)

### 模块化方案

整理出 [modular-design.md](hardware/modular-design.md)：

```
主板元件(板上):
  P4C6-Core + MP2315 + MT3608 + FPC30 + NT3H2111
  + Cherry MX×4 + WS2812B 5050×4 + RST
  + 上拉电阻 + 去耦电容

外购模块:
  MAX98357A I2S 功放模块 + ZTS6672 I2S 麦模块 ×2 + IPEX1 天线
```

主板 BOM 31 行（含外购 4 行），对比全分立 30 行但主板焊点从 ~50 降到 ~35。

### 文档产出

- [modular-design.md](hardware/modular-design.md) — 模块化设计完整方案
- 重写 [schematic-design.md](hardware/schematic/schematic-design.md)
- 重写 [final-pinmap.md](hardware/final-pinmap.md)
- 重写 [power-tree.md](hardware/power-tree.md)
- 更新 [bom.md](hardware/bom.md)、[bom-lcsc.csv](hardware/schematic/bom-lcsc.csv)、[block-diagram.md](hardware/block-diagram.md)
- 用户画出 39 个元件，通过网表交叉验证接线

### 当前状态

- ✅ 全部硬件文档同步
- ✅ 模块化方案定稿
- ✅ 引脚分配最终确认
- 🔧 原理图绘制中 (39/全量元件已放, 缺 MP2315/MT3608 等电源部分)

---

## 2026-07-31 (周四)

### 按键+RGB 方案变更: Cherry MX → 凯华轴座 + SK6812 3215 反贴灯

用户提出将按键和 RGB 方案从"Cherry MX 轴体直接焊板 + WS2812B 5050"改为可拔插的凯华热插拔轴座 + 3215 反贴灯方案。

**凯华轴座 6-pin 定义:**
| Pin | 信号 | 说明 |
|-----|------|------|
| 1 | VDD | LED 供电 (5V VBUS) |
| 2 | DOUT | LED 数据出 (→下一灯 DIN) |
| 3 | GND | LED 地 |
| 4 | DIN | LED 数据入 (←前灯 DOUT / GPIO51) |
| 5 | K0 | 开关端子 0 (→GPIO+上拉) |
| 6 | K1 | 开关端子 1 (→GND) |

**SK6812 3215 供电分析:**
- VDD_min=3.5V, 3.3V 供电蓝色通道不亮
- **改为 VBUS (5V) 直供**, 与 MAX98357A 共享 5V 轨
- 3.3V GPIO 直驱 5V SK6812: 实测可行 (输入阈值 ~2.5V), 保守可串 1N4148 降 VDD 至 ~4.3V
- **限流电阻: 不需要** (SK6812 内置恒流驱动, RGB 每通道独立恒流)
- 仅需: 数据线 33Ω 匹配电阻 + 每灯 100nF 旁路 + 总线 100µF

**接线方案:**
```
LED 级联: GPIO51─[33Ω]→LED1_DIN, LED1_DOUT→LED2_DIN, LED2_DOUT→LED3_DIN, LED3_DOUT→LED4_DIN(DOUT=NC)
按键: SW1-4 K0→GPIO1-4(+10kΩ 上拉), K1→GND
供电: 4灯 VDD 全部接 VBUS(5V) + 100µF 总去耦
```

### PCB 布局布线指南

用户询问 PCB 布线注意事项。编写了完整的 [pcb-layout-guide.md](hardware/pcb-layout-guide.md)，涵盖:
- **MIPI-DSI 差分对**: 100Ω 差分阻抗, 对内等长 ≤0.25mm, 同层不换层, 包地
- **C6 RF 天线**: 50Ω 单端, <15mm, 0 过孔, 圆弧拐角
- **USB DP/DM**: 90Ω 差分, FS 不严格
- **I2S**: ≤20mm 等长, 远离 DC-DC SW 节点
- **MP2315/MT3608 电源**: SW 节点最小化, FB 远离 SW, 输入电容紧贴 IC
- **NFC PCB 天线**: 3~4 匝 25×25mm, 下方禁铺地
- **4 层板推荐**: Top(Sig) / L2(GND) / L3(Pwr+Sig) / Bot(Sig)
- 嘉立创 EDA 操作要点

否决了用户调整 MIPI-DSI 引脚映射的想法 (时钟/数据物理通路不可互换)。

### D:\vscode 权限修复

整个 `D:\vscode` 目录 Users 组只有 RX 权限无 Write 权限，导致 Write 工具持续 EPERM。
执行 `icacls D:\vscode /grant Users:F /T` 修复。

### 文档全面更新

更新文件清单:
- [bom.md](hardware/bom.md): NT3H1101→NT3H2111, 轻触开关→凯华轴座, WS2812B→SK6812 3215, MP3302→MT3608
- [bom-lcsc.csv](hardware/schematic/bom-lcsc.csv): 新增 凯华轴座/SK6812/100nF×4/100µF/33Ω 行, BOM 30→35 行
- [schematic-design.md](hardware/schematic/schematic-design.md): Sheet 05 全重写, 全局 NT3H1101→NT3H2111, MP3302→MT3608, WS2812B→SK6812
- [schematic/README.md](hardware/schematic/README.md): 全量更新, 新增 SK6812/轴座/布线约束/pcb-layout-guide 引用
- [final-pinmap.md](hardware/final-pinmap.md): 按键/状态灯章节全重写, 供电架构更新
- [modular-design.md](hardware/modular-design.md): 按键/LED 模块描述重写, 系统框图更新, BOM 30→35
- [power-tree.md](hardware/power-tree.md): 全量重写 v1.1, SK6812 从 +3V3 移到 VBUS, MT3608 替 MP3302
- [block-diagram.md](hardware/block-diagram.md): Cherry MX→凯华轴座, WS2812B→SK6812
- **[pcb-layout-guide.md](hardware/pcb-layout-guide.md)** (新建): PCB 布局布线完整指南

### 当前状态

- ✅ 按键+RGB 方案改为凯华轴座 + SK6812 3215 反贴灯
- ✅ PCB 布局布线指南就绪
- ✅ 全部硬件文档已同步最新方案
- 🔧 原理图绘制中 (EDA 中需更新 Sheet 05 为新的轴座+反贴灯方案)

## 2026-08-02 (周日)

### 屏背光与供电核实 (YDP430BT009 手册)

pdftotext 成功提取屏手册全文, 确认背光 **8 LED 4S2P**, IF=20×2=40mA, Vf=10.8~13.6V (typ 12.2V)。
- **R5 背光采样电阻 5.1Ω→15Ω** (MT3608 恒流 0.6V/15Ω=40mA)
- 屏 VDD = 3.0~3.6V (typ 3.3V) → FPC VDD3.3V 直连 3V3, **无需 LDO**
- 触摸 I2C 标称 1.8V 电平, 3.3V 直连可行

### 全模块 datasheet 排查 (修正 3 处)

- **MP2315**: FB 基准确认 **0.8V** (非 0.6V) → 分压 100k/22k 改 **40.2k/12.7k** (原会输出 4.44V); VCC=0.1µF, BST=100nF+20Ω
- **MAX98357A**: GAIN 接 GND = **12dB** (原标 3dB 错误)
- **NT3H2111**: FD 开漏, 需 **4.7k 上拉** (原设计漏)
- 确认无误: MT3608 (FB 0.6V), ZTS6672 (SELECT 分声道), SK6812 (5V 直驱), 模组 88pin 与 pinmap 一致

### 接线方案文档

- 新建 [connection-fix.md](hardware/schematic/connection-fix.md): 逐脚接线方案 + 元件变更表

## 2026-08-03 (周一)

### 原理图网表复核 (断线全部修复)

- Netlist 08-03 复核: MP2315/MT3608/背光/屏 FPC/NFC 全部接好
- P0 修复: R4 接通 VBUS→+5V; SK6812 VDD 并入 +5V (5V 网络更名 +5V)
- P1 修复: 新增 IPEX U4 (LAN_OUT); I2C 上拉 R16/R17 (4.7k)
- 剩余 P2: I2S_MCLK 孤立 (GPIO5 可释放); GAIN=15dB (确认)

### 外设拓展方案

- 新建 [expansion.md](hardware/expansion.md): OV5640 MIPI 摄像头 + MPU-6500 六轴 + 舵机云台×2 + microSD (SDIO 4-bit)
- GPIO 分配: GPIO5(MCLK)/12(RSTB)/13(PWDN), 24/25(舵机), 39-44(SD), 47(MPU_INT), CSI 41-46
- 摄像头接口确认: ESP32-S3-CAM 摄像头是 **DVP 24pin**, 不能接 P4 MIPI-CSI → 弃用, 换 **MIPI 版 OV5640**
- 电源预算: 3V3 新增 ~250mA (总 ~1.45A < 2A); 舵机走 VBUS 5V + 470µF

### 外设拓展实装 (原理图, 08-03 下午)

**USB-A 外设口** (方案定稿 → 实装):
- USB2 (USB-A 母座 QHHTZB) + F1 (PPTC 500mA/16V) + D2 (USBLC6-2SC6)
- **FS OTG host 验证**: ESP-IDF v5.5.3 支持 P4 双 OTG, `usb_host_config_t.peripheral_map = BIT1` 选 FS 外设 (GPIO26/27, 模组 pin53/54); 源码依据 `soc/esp32p4/usb_dwc_periph.c` (controllers[1].internal_phy_io = dp27/dm26)
- 数据线用独立网络 **USB_A_DP / USB_A_DM** (不占用 HS 调试口 USB_DP/USB_DM)

**其他外设实装**:
- MPU-6500 (U7): I2C 0x68 + MPU_INT→GPIO47 + REGOUT 去耦
- microSD (TF1): SDIO 4-bit (GPIO39-44) + CD (GPIO45)
- 舵机 ×4 (U8-U11): SERVO1_YAW/GPIO24 + SERVO2_PITCH/GPIO25 驱动; SERVO3/GPIO28 + SERVO4/GPIO29 **预留** 3 线接口
- 功放 U1→**U12** (CH98357): 全 11 连接 (DIN/GAIN=GND/SD_MODE/BCLK/LRCLK/VDD=5V/OUTP/OUTN)

**网表审查修复**:
- USB2 D+/D- 网络合并: I/O1/I/O2 → USB_A_DP/USB_A_DM (USBLC6 pin1/6 与 pin3/4 内部合并)
- USB2 D+/D- **极性交叉**修复
- 功放重接 (原 U1 仅 GND)

**网表工具**:
- [tools/schematic_snapshot.py](tools/schematic_snapshot.py): 解析 epro2 (zip→epru, 按 DOCHEAD 分段取 SCH_PAGE/PCB) → 元件清单 + PAD_NET 网络
- [tools/schematic_review.py](tools/schematic_review.py): 自动审查 USB 极性 / 悬空网络 / 关键元件连接完整性

### 当前状态

- ✅ 原理图外设拓展实装 (USB 口 + MPU + SD + 舵机×4), 摄像头搁置
- ✅ 全板网表审查通过 (USB 极性/悬空/元件完整)
- ✅ 网表实时解析 + 自动审查工具就绪
- ✅ 文档同步: PROGRESS / WORKLOG / expansion / final-pinmap / BOM
- 🔧 PCB 布局; 摄像头待 MIPI 模块 pinout
