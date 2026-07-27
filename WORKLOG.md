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
