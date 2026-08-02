# vbcpet 项目进度报告

> 更新: 2026-08-03 | 当前阶段: Phase 1 硬件设计 — 断线已修复, 外设拓展定案, PCB 布局中

---

## 一、项目概述

vbcpet 是一个 AI 桌宠硬件项目。架构为 **PC 端渲染 AI/Live2D → USB 串口推流 → ESP32-P4 终端解码显示 + 按键/NFC/触摸交互**。

```
┌─ PC Host ───────────────────────────────────────────────────────┐
│  Claude Code (AI) → serial_bridge.py v0.2                       │
│  Playwright 无头浏览器 → Live2D 渲染 → JPEG 截图 → 串口推流      │
│  心情引擎: Claude状态→mood偏移→表情映射                          │
└──────────────────────┬──────────────────────────────────────────┘
                       │ USB 串口 (2M baud, 13.8fps)
┌──────────────────────┴──────────────────────────────────────────┐
│  ESP32-P4 WT9932P4-TINY (当前) / ESP32-P4C6-Core (最终)         │
│  ST7789V SPI 240×320 | 协议解析 | JPEG PPA硬解 | 文字叠加       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、当前进度

### ✅ 固件 (Firmware)

| 模块 | 状态 | 说明 |
|------|------|------|
| 模块化重构 | ✅ | 单片拆分为 7 模块: transport/channel_demux/display_pipeline/interaction_engine/text_render/text_input |
| SPI 显示 | ✅ | ST7789V, SPI Mode 0, 10MHz, 240×320 |
| JPEG 硬解 | ✅ | PPA 硬件解码, 单缓冲架构 (消除 DMA 竞态) |
| 串口推流 | ✅ | 13.8fps @ 2M baud, 12-14KB/帧 |
| 中文渲染 | ✅ | 7548 个 CJK 字形 (SimSun 16×16), UTF-8 解码, 透明背景 |
| 滚动显示 | ✅ | 自动滚动 + 新消息重置 |
| 行超时保护 | ✅ | 500ms 行超时自动重置 (防半行数据堆积) |
| 显示后端抽象 | ✅ | display_backend.h + SPI/DSI 双后端, Kconfig 编译时选择 |

### ✅ Live2D 角色系统

| 功能 | 状态 | 说明 |
|------|------|------|
| Hiyori 模型 | ✅ | Cubism 4, PixiJS 8 + @jannchie/pixi-live2d-display |
| Motion 动作 | ✅ | 10 个真实 motion 文件 (8-19KB, 31 参数曲线, 4.7s/30fps) |
| 表情预设 | ✅ | 5 个情绪 (neutral/happy/sad/surprised/angry), 13 个面部参数 |
| 调参工具 | ✅ | `live2d/tune_expressions.html` — 可视化滑块实时调参 |
| 心情系统 | ✅ | Mood 0-100 (基线 60), 右上角显示, Claude 状态→mood 偏移 |
| **动作队列 v3** | ✅ | idle~4.7s → 表情 2.5s → 循环, 原生眼眨+自然运动保留 |

### ✅ PC Host (serial_bridge.py v0.2)

| 功能 | 状态 | 说明 |
|------|------|------|
| Live2D 推流 | ✅ | Playwright 截图 → JPEG → 串口 |
| Claude 交互 | ✅ | 输入文字 → Claude → 回复显示到 P4 屏幕 |
| 按键模拟 | ✅ | [1][2][3][4] → yes/no/yes-always/panel |
| 心情引擎 | ✅ | Claude 状态偏移 + 用户行为偏移 + 闲置回归 + mood± 指令 |
| CTRL 队列化 | ✅ | asyncio.Queue 串行发送, 避免并发写冲突 |

### ✅ 硬件设计 (Hardware)

| 文档 | 状态 | 说明 |
|------|------|------|
| BOM | ✅ **全部定稿** | 5/5 ⚠️ 已解决 |
| 最终板引脚分配 | ✅ | [final-pinmap.md](hardware/final-pinmap.md) — P4C6-Core 88pin 完整分配 |
| 开发板引脚记录 | ✅ | [devboard-pinmap.md](hardware/devboard-pinmap.md) — WT9932P4-TINY 实际占用 |
| 原理图 | ✅ **断线已修复** | EasyEDA 网表复核通过, P0/P1 断线全部接好 |
| 外设拓展 | ✅ **方案定案** | OV5640 MIPI + MPU-6500 + 舵机×2 + microSD → [expansion.md](hardware/expansion.md) |
| PCB | 🔧 布局中 | 基础布局完成, 待加入外设元件 |

#### 原理图审计 (2026-08-02)

- **背光**: 屏手册确认 8 LED 4S2P, IF=20×2=40mA, Vf 10.8~13.6V → **R5 采样电阻 5.1Ω→15Ω** (MT3608 恒流 0.6V/15Ω=40mA)
- **屏供电**: 手册 3.2 节 Supply VDD 3.0~3.6V (typ 3.3V) → FPC VDD3.3V/TP_VDD 直连 3V3, **无需 LDO**
- **MP2315**: FB 基准确认 **0.8V** (非 0.6V) → 分压 **100k/22k 改 40.2k/12.7k** (原会输出 4.44V); VCC=0.1µF, BST=100nF+20Ω
- **MAX98357A**: GAIN 接 GND = **12dB** (原标 3dB 错误)
- **NFC**: NT3H2111 FD 开漏, **需 4.7k 上拉** (原设计漏)
- **PD**: 已砍 (纯 USB 5V), CC1/CC2 5.1kΩ 下拉
- **完整接线方案**: [connection-fix.md](hardware/schematic/connection-fix.md) ← 逐脚接线 + 元件变更表
- **08-03 网表复核**: MP2315/MT3608/背光/屏 FPC/NFC 已全部接好 ✅
- **08-03 二次复核**: P0×2(5V 通路 R4→+5V、SK6812 VDD 并入 +5V)+ P1×2(IPEX U4、I2C 上拉 R16/R17)全部修复 ✅
- **剩余 P2×2**: I2S_MCLK 孤立(GPIO5 可释放);GAIN=15dB(确认是否改 12dB)
- **待办**: 5V 网络更名 **+5V**, BOM/文档需同步;IPEX 地脚 PCB 阶段铺地
- **08-03 外设拓展**: 新增 OV5640 摄像头(MIPI-CSI) + MPU-6500 六轴 + 舵机云台×2 + microSD(SDIO 4-bit) → [expansion.md](hardware/expansion.md)

### 🔧 待优化

| 项目 | 说明 |
|------|------|
| ST7789V 颜色校正 | 面板硬件色偏, 需软件色表映射 |
| Live2D 锯齿 | 模型渲染 ~76×107 像素过小, MSAA/SSAA 无效, 需调大 scale 或重构图 |
| FPS 提升 | 试 3M baud (目标 >18fps) |

### ⏸️ 暂缓 (WT9932P4-TINY 无对应硬件)

| 模块 | 原因 |
|------|------|
| WiFi (C6) | 开发板无 C6, 最终板 P4C6-Core 内置 |
| 音频 (ES7210/ES8311) | 开发板无 codec |
| 按键 | 未焊接 |
| MIPI-DSI 显示 | 开发板只有 SPI, 最终板用 YDP430BT009-V1 |

---

## 三、BOM 选型决策

| # | 项目 | 决策 | 日期 |
|---|------|------|------|
| 1 | 屏幕 | **YDP430BT009-V1** (鱼鹰光电), 4.3" IPS 480×800, ST7102 MIPI-DSI + 触摸 | 7/28 |
| 2 | PA 功放 | **NS4150**, GPIO 控制, 3W D 类 | 7/28 |
| 3 | 在位检测 | **PN532 NFC** (I²C), 手机放底座自动识别 | 7/28 |
| 4 | PD 供电 | **CH224K** 请求 9V, 5V 回退; 板载 3.3V Buck + 1.8V LDO | 7/28 |
| 5 | 主控 | **ESP32-P4C6-Core** (鱼鹰光电), P4+C6 一体模组, 88pin | 7/28 |

### I²C 地址分配

| 设备 | 地址 | 总线 |
|------|------|------|
| ES8311 (DAC) | 0x18 | I²C0 |
| ES7210 (ADC) | 0x40 | I²C0 |
| PN532 (NFC) | 0x24 | I²C0 |
| 触摸 (ST7102 内置) | (待确认) | I²C0 → TXS0102 → 1.8V |

### 供电架构

```
USB-C → CH224K (PD 9V) → DC-DC Buck → 3.3V @ 2A → P4C6-Core + 外设
                        → LDO → 1.8V (触摸 IO / codec IO)
                        → Boost LED Driver → ~26V (屏背光)
```

---

## 四、关键问题与解决

### 1. PIXI v8 + Cubism SDK WebGL 冲突 (7/28)

**现象**: 模型加载成功但不显示, `bindBuffer: element array buffers can not be bound` × 1635

**根因**: `@jannchie/pixi-live2d-display` 1.3.0 + PIXI 8.19 + Cubism Core 5.1.0 底层 WebGL 状态冲突。`doUpdateMotion` → `updateParameters` 阶段 buffer 绑定与 PIXI 渲染管线不一致。`tune_expressions.html` 正常是因为禁用了所有动画。

**解决**: 动作队列架构 — 不持续跑 idle, 改用 idle→表情→idle 队列循环。保留原生眼眨+自然运动。

### 2. 表情被 idle 动画覆盖 (7/27)

**现象**: `applyEmotion()` 设完参数后, idle 动画每帧覆盖 → 表情仅持续 5 秒

**解决**: 改为 `applyEmotion` 先 `stopAllMotions()` → 过渡参数 → 再恢复 idle。(后被动作队列 v3 替代)

### 3. Live2D 参数 API 陷阱 (7/27)

**现象**: `internalModel.parameterSetValue` / `setParameterValueById` 均不存在

**解决**: 正确 API 在 top-level `model` 对象上: `model.setParameterValue(id, value)` / `model.getParameterValue(id)`

### 4. ESP-IDF idf.py 不编译 (7/27)

**现象**: Git Bash 中 idf.py 返回 0 但无编译输出

**根因**: `idf.py` 检测 `MSYSTEM` 环境变量后仅打印警告, 跳过 `main()`。解决: `set "MSYSTEM="` 批处理绕过。

### 5. 显示稳定性 (6 个连环问题, 7/23-24)

| 问题 | 根因 | 修复 |
|------|------|------|
| 花屏 | g_buf_idx 初始值 bug | feed_jpeg 内部 flush |
| 心跳阻塞 | USB Serial JTAG TX/RX 冲突 | 非阻塞 send + 加大 TX 缓冲 |
| SPI DMA 溢出 | 每循环都 flush | 只在有新帧时 flush |
| JPEG+文字花屏 | DMA 未完成时写同一内存 | 单缓冲: 解码→文字→一次 flush |
| 文字仅显示一帧 | g_text_dirty 标志 | 去掉 dirty, 每帧叠加 |
| 字符变黑色方块 | draw_ascii 画了背景像素 | 透明背景渲染 |
| 长消息 JSON 截断 | 行缓冲 256B 限制 | 256→512→1024B + 500ms 行超时 |

---

## 五、Live2D 系统架构演进

```
v1 (7/27): applyEmotion → stopAllMotions → 过渡表情 → 5s后恢复idle
          问题: 5秒后表情消失

v2 (7/28 morning): motionManager.update hook → 每帧叠加表情
          问题: WebGL buffer 冲突 ×1635

v3 (7/28 final): 动作队列 — startRandomMotion 拦截 → 手动队列循环
          idle ~4.7s → isFinished() → 表情 2.5s → 下一 idle → ...
          保留原生眼眨+自然运动, 表情参数直接控制
```

---

## 六、技术摘要

### 当前开发板 (WT9932P4-TINY)

```
ESP32-P4 rev1.3 | SPI: Mode 0, 10MHz | Display: 240×320 ST7789V
Console: UART0 | Data: USB Serial JTAG (COM16, 2M baud)
Flash: bootloader@0x2000 (⚠️ P4 专有!) | partition@0x8000 | app@0x10000
Protocol: FRAME:<hex_size>\n + JPEG | Quality: 80 | 13.8fps, 12-14KB/帧
单缓冲: 解码→文字叠加→flush (消除 DMA 竞态)
```

### 最终板 (ESP32-P4C6-Core)

```
P4 双核 360MHz + C6FH4 WiFi6/BLE5 | 32MB PSRAM + 16MB Flash
MIPI-DSI 2-lane → ST7102 480×800 | I²C 触摸 1.8V | PN532 NFC
I2S → ES7210(ADC) + ES8311(DAC) + NS4150(PA)
PD 9V → 3.3V Buck + 1.8V LDO + 背光升压
```

### 引脚变化 (开发板 → 最终板)

| 功能 | 开发板 | 最终板 | 原因 |
|------|--------|--------|------|
| 显示接口 | SPI IO16-20 | MIPI-DSI 34-39 | 换 DSI 屏 |
| I2C SCL | IO18 | IO21 | IO18 模组未引出 |
| I2S BCLK | IO16 | IO22 | IO16 模组未引出 |
| I2S DOUT | IO15 | IO23 | IO15 模组未引出 |
| LCD RST | IO18 | IO20 | 独立 GPIO |

---

## 七、文件结构

```
vbcpet/
├── firmware/                    # P4 固件 (ESP-IDF v5.5.3)
│   └── main/
│       ├── vbcpet_main.c        # 主入口
│       ├── transport.c          # USB 串口收发
│       ├── channel_demux.c      # 协议分流 + 行超时
│       ├── display_pipeline.c   # JPEG 解码 + 帧管理
│       ├── display_pipeline_spi.c  # ST7789V 后端
│       ├── display_backend.h    # 显示抽象接口
│       ├── interaction_engine.c # 交互处理
│       ├── text_render.c        # 中文渲染 + 滚动
│       ├── text_input.c         # 文字输入
│       └── serial_display.c     # 旧版推流 (已替换)
├── pc-host/                     # PC 端
│   ├── serial_bridge.py         # ★ 主控: 推流 + Claude + 表情 + 心情
│   ├── claude_sdk_driver.py     # Claude SDK 驱动
│   ├── agent_driver.py          # 回调接口定义
│   └── stream_test.py           # 纯推流压测
├── live2d/                      # Live2D 渲染
│   ├── render.html              # ★ 角色渲染 + 心情系统 + 动作队列
│   ├── tune_expressions.html    # ★ 表情调参工具
│   ├── server.py                # HTTP 服务器
│   └── Hiyori/                  # 角色模型 + 纹理 + 10 真实 motion
├── hardware/                    # 硬件设计
│   ├── bom.md                   # ★ 物料清单 (全部定稿)
│   ├── final-pinmap.md          # ★ 最终板引脚分配
│   ├── devboard-pinmap.md       # 开发板引脚占用
│   ├── pinmap.md                # 引脚规划 (含 DSI 屏 FPC)
│   ├── expansion.md             # ★ 外设拓展 (摄像头/六轴/舵机/SD)
│   ├── power-tree.md            # 电源树
│   ├── block-diagram.md         # 系统框图
│   ├── phase0-validation.md     # Phase 0 验证清单
│   ├── phase0-report.md         # Phase 0 验证报告
│   └── development-plan.md      # 全流程开发方案
├── docs/
│   ├── protocol.md              # DataChannel 协议 v1
│   └── decisions.md             # ADR 架构决策
├── WORKLOG.md                   # 每日工作日志
└── PROGRESS.md                  # ← 本文件
```

---

## 八、下一步

| 优先级 | 任务 | 说明 |
|--------|------|------|
| **P0** | 加入外设元件 (EasyEDA) | 按 [expansion.md](hardware/expansion.md): 摄像头座/MPU-6500/舵机座/microSD + LDO (等 MIPI 摄像头 pinout) |
| P1 | ERC + BOM 同步 | 清零孤立网络 (GPIO5 MCLK), 位号 R5/R14、+5V 命名同步 |
| P1 | 固件引脚适配 | IO15/16/18 → IO21/22/23 |
| P1 | P4 硬件端到端验证 | serial_bridge.py + 心情表情联动 |
| P2 | ST7789V 颜色校正 | 软件色表映射 |
| P2 | 调参工具刷新表情预设 | 用户在 tune_expressions.html 微调后同步到 render.html |

### 预计时间线

| 阶段 | 时间 | 状态 |
|------|------|------|
| Phase 0 Bring-up (推流+显示) | 1-2 周 | ✅ 完成 |
| Phase 0 Live2D (模型+表情+心情) | 1 周 | ✅ 完成 |
| BOM 定稿 + 引脚分配 | — | ✅ 完成 (7/28) |
| Phase 1 原理图 (EasyEDA) | 2-3 周 | 🔧 断线已修复, 外设拓展中 |
| Phase 2 PCB | 3-4 周 | ⬜ |
| Phase 3-4 打样+Bring-up | 3-5 周 | ⬜ |
