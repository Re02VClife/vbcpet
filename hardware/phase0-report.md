# Phase 0 验证报告 — vbcpet ESP32-P4 Bring-up

> 日期: 2026-07-17 | 开发板: WT9932P4-TINY | ESP-IDF: v5.5.3

---

## 总体状态

| 模块 | 状态 | 备注 |
|------|------|------|
| NVS | ✅ OK | flash 初始化正常 |
| 协议 (DataChannel) | ✅ 24/24 PASS | 构建/解析/v1 event 编解码全通过 |
| SPI 显示 (ST7789V) | ⚠️ 能用有色偏 | 绿→红，红→黄，蓝→紫；MADCTL=0x60 方向OK |
| USB 串口推流 | ✅ 数据链路通 | PC→P4 JPEG 帧传输稳定 @ 3.6fps/12.7KB/s |
| JPEG 硬件解码 | 🔧 调试中 | 缓冲区对齐问题，内存分配失败 |
| Live2D 渲染 (PC) | ✅ OK | PixiJS + Cubism 4 + Playwright 无头截图可用 |
| WiFi (C6) | ❌ 未开始 | WT9932P4-TINY 无 C6 模块，需外挂 |
| 音频 (ES7210/ES8311) | ❌ 未开始 | 板子无音频 codec |

---

## P0-1 显示出图 (SPI ST7789V)

**状态**: ⚠️ 能用但颜色不对

**引脚配置** (对应 WT9932P4-TINY):
```
CS=IO16, DC=IO17, RST=IO18, MOSI=IO19, SCK=IO20, BL=不接
SPI Mode 0, 10MHz
```

**关键发现**:
1. ST7789V 寄存器 (MADCTL/COLMOD/INVON) 必须在关屏后写入，否则返回 ESP_OK 但不生效
2. MADCTL=0x60 (MX=1, MV=1) 实现 320×240 横屏，方向正确
3. 颜色存在系统性偏差：绿背景→红，红→黄，蓝→紫。调整 RGB 顺序、INVON、COLMOD 均无法修正，可能是面板硬件固定色映射

**待解决**: 颜色校正（可能需要软件色表映射）

---

## P0-2 PC→P4 视频推流 (替代 WebRTC/H.264)

**状态**: 🔧 数据链路已通，解码/显示调试中

**架构** (因无 WiFi，使用 USB 串口替代):
```
PC (Playwright 无头浏览器) → JPEG 截图 → COM16 (2M baud) → P4 (USB Serial JTAG) → PPA JPEG 解码 → ST7789V
```

**协议**:
```
PC → P4: "FRAME:<hex_size>\n" + <jpeg_bytes>
```

**已验证**:
- PC→P4 串口帧传输: 3.6 fps, JPEG 平均 ~3.7KB, 带宽 ~12.7KB/s
- USB Serial JTAG RX 驱动层通信正常（`usb_serial_jtag_read_bytes`）
- Playwright 无头截图正常（320×240，JPEG quality=55）

**关键发现**:
1. USB Serial JTAG RX **必须**安装驱动 (`usb_serial_jtag_driver_install`)，否则 `usb_serial_jtag_read_bytes` 崩溃
2. 不能使用 `usb_serial_jtag_ll_read_rxfifo`（LL 层直接读硬件 FIFO）— ISR 已将数据搬到驱动 ring buffer，FIFO 为空
3. Console 必须配置在 UART0（`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`），USB Serial JTAG 专用于数据，否则 ESP_LOGI 与数据流混合导致反压
4. JPEG 解码输出缓冲必须用 `jpeg_alloc_decoder_mem` 分配（对齐要求），`malloc` 不满足硬件对齐 → 解码失败 (error 258)
5. 150KB 输出缓冲动态分配失败（内存碎片），需启动时预分配

**待解决**:
- 预分配缓冲后验证 JPEG 解码 + ST7789V 显示端到端
- 提高帧率（当前 3.6fps，目标 ≥12fps）
- 换用 render.html 显示 Live2D 角色

---

## P0-3 音频

**状态**: ❌ 未开始
- WT9932P4-TINY 无 ES7210/ES8311，需要外接

---

## P0-4 按键 + 授权回路

**状态**: ❌ 未开始
- 4 按键未连接
- 协议层面已就绪（`vbc_build_button` / `vbc_parse_prompt_confirm` 已测试通过）

---

## P0-5 C6 联网

**状态**: ❌ 未开始
- WT9932P4-TINY 无 C6 模组，需通过 SDIO/SPI 外挂

---

## 文件变更

| 文件 | 说明 |
|------|------|
| `firmware/main/serial_display.c` | **新增** — USB 串口 JPEG 接收 + 解码 + 显示 |
| `firmware/main/serial_display.h` | **新增** — 头文件 |
| `firmware/main/display_test.c` | 静态测试图案（已不用） |
| `firmware/main/vbcpet_main.c` | 启动流程：NVS → serial_display_start() |
| `firmware/main/CMakeLists.txt` | 添加 serial_display.c + esp_driver_jpeg 依赖 |
| `firmware/sdkconfig.defaults` | Console=UART0, USB Serial JTAG 专用于数据, chip rev v1 |
| `pc-host/serial_stream.py` | **新增** — PC 端 Live2D 渲染 + JPEG 截图 + 串口推流 |
| `live2d/render.html` | 320×240 适配 + 模型参数调整 |
| `live2d/test.html` | **新增** — Canvas 2D 测试图案（调试用） |

---

## 下一步

1. **修 JPEG 解码**：预分配缓冲后验证端到端显示
2. **验证 Live2D 显示**：切回 render.html，确认 Hiyori 角色可见
3. **颜色校正**：ST7789V 色偏（可能是面板硬件特性）
4. **性能优化**：提高帧率（目标 12fps+）
5. **WiFi + C6**：评估外挂 C6 模组方案
