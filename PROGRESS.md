# vbcpet 项目进度报告

> 更新: 2026-07-17 | 当前阶段: Phase 0 Bring-up

---

## 一、项目概述

vbcpet 是一个 AI 桌宠硬件项目。架构为 **PC 端渲染 AI/Live2D → 网络推流 → ESP32-P4 终端解码显示 + 按键/触摸交互**。

```
┌─ PC Host ──────────────────────────────────────────────────────┐
│  Claude Code (AI) → 导演/压缩 → Live2D 渲染 → WebRTC/H.264 推流  │
│  host.py / serial_stream.py / live2d_track.py                  │
└──────────────────────┬─────────────────────────────────────────┘
                       │ WiFi (计划) / USB 串口 (当前)
┌──────────────────────┴─────────────────────────────────────────┐
│  ESP32-P4 WT9932P4-TINY 终端                                    │
│  ST7789V SPI 显示 | 协议解析 | JPEG 硬解 | 按键输入 (计划)       │
└────────────────────────────────────────────────────────────────┘
```

---

## 二、当前进度

### ✅ 已完成

| 模块 | 详情 |
|------|------|
| **固件环境** | ESP-IDF v5.5.3 + ESP32-P4 rev1.3, 编译/烧录/串口监控环境就绪 |
| **NVS** | Flash 存储初始化正常 |
| **协议栈** | DataChannel 协议 **24/24 测试通过** — 构建(hello/heartbeat/button/dock/user_text)、解析(panel/say/type/work_status/prompt_confirm)、v1 CharacterEvent 编解码、v0/v1 兼容层 |
| **SPI 显示** | ST7789V 320×240 点亮，SPI Mode 0 / 10MHz，方向正确 (MADCTL=0x60) |
| **USB 串口推流** | PC → P4 JPEG 帧传输链路通，稳定 3.6fps / 12.7KB/s |
| **PC Live2D 渲染** | PixiJS + Cubism 4 + Playwright 无头截图可用 (Hiyori 模型就位) |

### 🔧 调试中

| 模块 | 状态 | 阻塞点 |
|------|------|--------|
| **JPEG 硬解 + 显示** | 数据链路通 | 缓冲区对齐要求 → `jpeg_alloc_decoder_mem` 分配失败/预分配修复中 |
| **ST7789V 颜色** | 能显示但偏色 | 绿→红、红→黄、蓝→紫，所有软件配置无效，疑似面板硬件固有色映射 |

### ❌ 未开始

| 模块 | 原因 |
|------|------|
| WiFi (C6) | WT9932P4-TINY 无 C6 模组，需外挂 |
| 音频 (ES7210/ES8311) | 板子无音频 codec |
| 按键输入 | 未连接物理按键 |
| WebRTC/H.264 | 依赖 WiFi，当前用 USB 串口临时替代 |

---

## 三、遇到的关键问题与解决

### 1. USB Serial JTAG 接收数据 (耗时最长 ⚠️)

**现象**: PC 通过 COM16 发送数据，P4 端始终收不到，`ser.write()` 超时

**排查过程**:
1. ❌ `usb_serial_jtag_read_bytes` 无驱动 → 崩溃 (Load access fault)
2. ❌ 安装驱动后 → 不崩溃但收不到数据
3. ❌ 改用 `fgetc(stdin)` + O_NONBLOCK → 非阻塞模式在 USB CDC 上不生效
4. ❌ 改用 `usb_serial_jtag_ll_read_rxfifo` 直读硬件 FIFO → ISR 已将数据搬到驱动 ring buffer，FIFO 为空
5. ✅ **根因**: Console 配置为 USB Serial JTAG 时，ESP_LOGI 与数据流共享同一 USB 通道，日志填满 IN 缓冲区导致反压。**解决**: console 切回 UART0 (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`)，USB Serial JTAG 专用于数据

**教训**: ESP32-P4 的 USB Serial JTAG 是独立硬件，与 UART0 console 分离。数据传输场景必须将 console 配置在 UART0，不可共用 USB 通道。

### 2. JPEG 硬件解码失败

**现象**: `jpeg_decoder_process` 报错 "output buffer size is not aligned"

**解决**: `malloc` 分配的缓冲区不满足 PPA JPEG 硬件对齐要求，必须用 `jpeg_alloc_decoder_mem` 分配（内部使用 DMA 对齐 + cache line alignment）。后续又遇到 150KB 大缓冲动态分配失败，改为启动时预分配+复用。

### 3. ST7789V 寄存器写入不生效

**现象**: MADCTL/COLMOD/INVON 写操作返回 ESP_OK 但显示无变化

**根因**: ST7789V 寄存器写入必须在 display OFF 状态下进行。`esp_lcd_panel_disp_on_off(false)` → 写寄存器 → `esp_lcd_panel_disp_on_off(true)`

### 4. ST7789V 颜色系统偏差

**现象**: 绿背景显示为红，红显示为黄，蓝显示为紫

**尝试过**: 调整 RGB/BGR 顺序、INVON 反转、COLMOD 像素格式、SPI Mode 切换 — 均无效。色偏为系统性的，所有颜色按固定方式偏移。暂定为面板硬件特性，需软件色表映射修正。

### 5. Live2D 模型不在截图内

**现象**: render.html 画布 480×800 但截图 viewport 320×240，模型在 y=400 被截掉

**解决**: 画布改为 320×240 匹配显示屏，模型缩放和位置相应调整

---

## 四、技术摘要

### 关键配置

```
ESP32-P4 rev1.3 | SPI: Mode 0, 10MHz | Display: 320×240 RGB565
Console: UART0 (GPIO 37/38) | Data: USB Serial JTAG (COM16, 2M baud)
Protocol: FRAME:<hex_size>\n + JPEG | JPEG: quality 55, ~3.7KB/frame
```

### 内存使用

```
Total: ~545 KiB (143+18+384 RETENTION+RAM)
JPEG 解码输出: 153,600 bytes (~28%)
固件大小: 334,592 bytes (flash, 70% free)
```

---

## 五、下一步规划

### 短期 (本周)

1. **修 JPEG 解码显示** — 预分配缓冲后验证端到端，让 Live2D 或测试图案出现在屏幕上
2. **颜色校正** — 软件 RGB565 色表映射绕过硬件色偏
3. **帧率提升** — 目标从 3.6fps → 12fps+（优化 JPEG 压缩参数、调高 SPI 时钟到 40MHz）

### 中期 (1-2周)

4. **WiFi 联网** — 评估外挂 ESP32-C6 模组方案 (SDIO/SPI)，打通 WebRTC/H.264 推流替代 USB 串口
5. **按键输入** — 焊接 4 按键 (yes/no/yes-always/panel)，打通工具授权闭环
6. **Live2D 角色切换** — 完善 PC 端渲染，支持动作/表情控制

### 长期

7. **音频链路** — ES7210 + ES8311 录放 + AEC + Opus (需硬件支持)
8. **Phase 1 原理图** — KiCad 7 张 Sheet 设计 (电源/P4-Core/C6-Radio/显示/音频/IO/USB)
9. **Phase 2 PCB** — 4 层板布局布线，打样 5 片
10. **Phase 3-4 打样 + Bring-up** — 上电分区排查，闭环测试
11. **Phase 5 迭代** — v1.x 改板，量产考虑

### 预计总时间线

| 阶段 | 时间 | 状态 |
|------|------|------|
| Phase 0 Bring-up | 1-2 周 | 🔧 进行中 |
| Phase 1 原理图 | 2-3 周 | ❌ 待开始 |
| Phase 2 PCB | 3-4 周 | ❌ 待开始 |
| Phase 3-4 打样+Bring-up | 3-5 周 | ❌ 待开始 |
| **预计总计** | **10-17 周** | |

---

## 六、文件结构

```
vbcpet/
├── firmware/                  # P4 固件 (ESP-IDF)
│   ├── main/
│   │   ├── vbcpet_main.c      # 主入口
│   │   ├── serial_display.c   # USB 串口 JPEG 接收+解码+显示
│   │   ├── display_test.c     # 静态测试图案
│   │   ├── protocol.c         # DataChannel 协议 (构建+解析)
│   │   └── CMakeLists.txt
│   └── sdkconfig.defaults
├── pc-host/                   # PC 端 (Python)
│   ├── host.py                # WebRTC 信令 + 视频轨
│   ├── serial_stream.py       # USB 串口 Live2D 推流
│   ├── live2d_track.py        # Playwright 无头 Live2D 截图
│   └── claude_sdk_driver.py   # Claude SDK 驱动
├── live2d/                    # Live2D 渲染 (PixiJS)
│   ├── render.html            # 主渲染页 (Cubism 4)
│   ├── test.html              # Canvas 2D 测试页
│   └── Hiyori/                # 角色模型
├── hardware/                  # 硬件设计文档
│   ├── phase0-report.md       # Phase 0 验证报告
│   ├── development-plan.md    # 开发方案
│   ├── bom.md / pinmap.md / power-tree.md
│   └── block-diagram.md
└── docs/
    ├── protocol.md            # DataChannel 协议 v1
    └── decisions.md           # ADR 架构决策
```
