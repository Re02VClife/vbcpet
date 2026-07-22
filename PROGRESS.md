# vbcpet 项目进度报告

> 更新: 2026-07-21 | 当前阶段: Phase 0 Bring-up — ✅ 端到端推流 14fps 达成，P4 性能充裕

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
| **SPI 显示** | ST7789V 240×320 竖屏，SPI Mode 0 / **40MHz**，MADCTL=0x00 |
| **JPEG 硬解** | PPA 硬件解码 + 双缓冲，解码 ~1ms/帧，理论可到 1000fps |
| **PC Live2D 渲染** | PixiJS + Cubism 4 + Playwright (Edge + SwiftShader)，240×320 画布 |
| **USB 串口推流** | PC → P4 JPEG 帧传输，协议 `FRAME:<hex_size>\n` + JPEG，2M baud |
| **Live2D → 屏幕** | 端到端全链路通：PC渲染→JPEG→串口→P4解码→显示，**14.2fps @ quality 80** |
| **启动画面** | 默认十字准星 JPEG 内嵌固件，开机即显示 |
| **P4 性能** | decode=1ms, SPI draw=0ms (DMA异步), 瓶颈在PC端串口传输(~488KB/s有效) |
| **FPS 计数器** | 屏幕左上角实时帧率显示 |
| **P4 bootloader** | ESP32-P4 专有 flash 偏移 0x2000 已确认并文档化 |
| **问题排查报告** | `debug-report.md` — 5 个根因完整记录 |

### 🔧 待优化

| 模块 | 状态 | 说明 |
|------|------|------|
| **ST7789V 颜色校正** | 已知硬件偏色 | 绿→红、红→黄、蓝→紫，所有软件配置无效，需软件色表映射 |

### ⏸️ 暂缓 (WT9932P4-TINY 无对应硬件)

| 模块 | 原因 |
|------|------|
| WiFi (C6) | WT9932P4-TINY 无 C6 模组，需外挂模组或换开发板 |
| 音频 (ES7210/ES8311) | 板子无音频 codec，需外接 |
| 按键输入 | 未连接物理按键，需焊接 |
| WebRTC/H.264 | 依赖 WiFi + 开发板音频，当前 USB 串口 + JPEG 已验证链路可行 |

### 📋 下一步 — 按优先级排序

1. **代码整理 + Git 提交** — 清理临时文件，提交已验证的固件和推流代码
2. **硬件升级方案评估** — WT9932P4-TINY 功能受限，决定是外挂模块还是换开发板
3. **ST7789V 颜色校正** — 软件 RGB565 色表映射
4. **帧率进一步提升** — 提高串口波特率到 3M/4M，或评估 WiFi 替代方案

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

### 6. ESP32-P4 bootloader flash 偏移量 (2026-07-20 发现 🔑)

**现象**: 重新烧录后 P4 无限循环 `invalid header: 0x5073bcb7` + WDT 重启，esptool 读回 flash 数据正确但 ROM bootloader 找不到有效固件

**根因**: **ESP32-P4 与其他 ESP32 不同 — ROM bootloader 在 flash 偏移 `0x2000` 处查找第二级 bootloader**，而非标准 ESP32 的 `0x0`。手动 esptool 命令用错偏移量（`0x0`），而 `idf.py flash` 自动读取 `flasher_args.json` 使用正确偏移。

**证据**: `firmware/build/flasher_args.json` 明确记录 `"0x2000": "bootloader/bootloader.bin"`

**正确烧录命令**:
```bash
esptool write_flash 0x2000 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 vbcpet.bin
# 或直接用: idf.py flash  (自动使用正确偏移量)
```

---

## 四、技术摘要

### 关键配置

```
ESP32-P4 rev1.3 | SPI: Mode 0, 40MHz | Display: 240×320 RGB565 (竖屏 MADCTL=0x00)
Console: UART0 | Data: USB Serial JTAG (COM16, 2M baud)
Flash: bootloader@0x2000 (⚠️ ESP32-P4 专有!) | partition@0x8000 | app@0x10000
Protocol: FRAME:<hex_size>\n + JPEG | Quality: 80 | Live2D: ~14KB/frame, 14.2fps
P4 timing: decode=1ms, SPI=0ms (DMA), bottleeck=PC串口传输 (488KB/s @ 2M baud)
双缓冲: JPEG_alloc_decoder_mem × 2, 交替解码/DMA刷屏
推流脚本: stream_simple.py (无 drain 线程, Playwright 直接 JPEG 截图)
```

### 内存使用

```
Total: ~545 KiB (143+18+384 RETENTION+RAM)
JPEG 解码输出: 153,600 bytes (~28%)
固件大小: 334,592 bytes (flash, 70% free)
```

---

## 五、下一步规划

### 立即可做

1. **代码整理 + Git 提交** — 清理临时脚本文件，提交已验证的固件+推流代码。当前未跟踪文件：
   - `debug-report.md` (排查报告) → 应该提交
   - `firmware/main/default_screen.h` (嵌入式启动画面) → 应该提交
   - `pc-host/stream_simple.py` (主力推流脚本) → 应该提交
   - `pc-host/crosshair_240x320.jpg`, `grid_test.jpg`, `quad_test.jpg`, `red_test.jpg` → 清理或移入 test-assets/
   - `_tmp_build.*` → 删除

2. **ST7789V 颜色校正** — 软件 RGB565 色表映射

### 关于硬件方案的讨论

当前 WT9932P4-TINY 开发板是 P4 最小系统板，**缺少 Phase 0 验证清单中的关键外设**：
- 无 ESP32-C6 (WiFi6)
- 无音频 codec
- 无物理按键
- 无 DSI 接口 (只有 SPI 小屏)

有两条路可以走：

| 路线 | 做法 | 优势 | 劣势 |
|------|------|------|------|
| **A: 外挂模块** | 在 WT9932P4-TINY 上飞线接 C6 模组 + 音频模块 + 按键 | 利用现有板子，成本低 | 飞线多、稳定性差、SPI 屏带宽有限 |
| **B: 换开发板** | 上 ESP32-P4-Function-EV-Board (官方全功能板) | 原生 DSI 大屏、C6、音频、按键都有 | 成本高(~¥500+)、板子大 |

**建议先走路线 A 验证关键链路（C6 WiFi + 数据吞吐），再决定是否投入 Phase 1 画板。**

### 中期 (1-2周)

3. **WiFi 联网验证** — 外挂 ESP32-C6 模组 (SDIO/SPI)，测 WiFi6 吞吐，对比 USB 串口方案
4. **按键输入** — 焊接 4 按键 (yes/no/yes-always/panel)，打通工具授权闭环
5. **Live2D 角色交互** — 动作/表情控制指令

### 长期

6. **音频链路** — ES7210 + ES8311 录放 + AEC + Opus
7. **Phase 1 原理图** — KiCad 7 张 Sheet 设计
8. **Phase 2 PCB** — 4 层板布局布线

### 预计总时间线

| 阶段 | 时间 | 状态 |
|------|------|------|
| Phase 0 Bring-up (SPI显示+JPEG+Live2D推流) | 1-2 周 | ✅ 基本完成 |
| Phase 0 Bring-up (WiFi/音频/按键) | 2-3 周 | ⏳ 待硬件 |
| Phase 1 原理图 | 2-3 周 | ❌ 未开始 |
| Phase 2 PCB | 3-4 周 | ❌ 未开始 |
| Phase 3-4 打样+Bring-up | 3-5 周 | ❌ 未开始 |
| **预计总计** | **10-17 周** | |

---

## 六、文件结构

```
vbcpet/
├── firmware/                  # P4 固件 (ESP-IDF)
│   ├── main/
│   │   ├── vbcpet_main.c      # 主入口 — 显示初始化 + JPEG解码 + 串口帧接收 + 双缓冲
│   │   ├── display_test.c     # ST7789V SPI 显示驱动 (40MHz, 竖屏 MADCTL=0x00)
│   │   ├── display_test.h     # + display_test_get_panel()
│   │   ├── serial_display.c   # 原始实现 (当前未编译，CMakeLists 用 display_test.c)
│   │   ├── default_screen.h   # 内嵌十字准星 JPEG 启动画面
│   │   ├── protocol.c         # DataChannel 协议 (构建+解析)
│   │   └── CMakeLists.txt     # 依赖: driver esp_lcd esp_driver_jpeg esp_timer
│   └── sdkconfig.defaults
├── pc-host/                   # PC 端 (Python)
│   ├── host.py                # WebRTC 信令 + 视频轨
│   ├── stream_simple.py       # ★ 主力推流 — 无 drain 线程, Playwright JPEG 截图, quality 80
│   ├── serial_stream.py       # 旧版推流 (有 drain 线程问题)
│   ├── live2d_track.py        # Playwright 无头 Live2D 截图
│   └── claude_sdk_driver.py   # Claude SDK 驱动
├── live2d/                    # Live2D 渲染 (PixiJS)
│   ├── render.html            # 240×320 竖屏渲染 (Cubism 4 + FPS计数器)
│   ├── test.html              # Canvas 2D 测试页
│   └── Hiyori/                # 角色模型
├── hardware/                  # 硬件设计文档
│   ├── development-plan.md    # 开发方案 (全流程)
│   ├── phase0-validation.md   # Phase 0 验证清单
│   ├── bom.md / pinmap.md / power-tree.md
│   └── block-diagram.md
├── docs/
│   ├── protocol.md            # DataChannel 协议 v1
│   └── decisions.md           # ADR 架构决策
├── debug-report.md            # 显示调试五大根因排查报告
└── PROGRESS.md                # ← 本文件
```
