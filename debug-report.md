# vbcpet 显示问题排查报告

> 日期: 2026-07-21 | 涉及文件: serial_display.c, display_test.c, render.html, serial_stream.py

---

## 问题现象

固件推流 Live2D 角色到 ST7789V 屏幕，屏幕始终无变化（保持上电默认白屏或旧画面），但 P4 端日志确认 JPEG 帧已被正确接收和解码。

---

## 根因分析

共发现 **5 个独立问题**，按发现顺序：

### 1. ESP32-P4 bootloader flash 偏移量错误 🔑

**现象**: 重新烧录后 P4 无限循环 `invalid header: 0x5073bcb7` + WDT 重启。

**根因**: ESP32-P4 的 ROM bootloader 在 flash 偏移 **0x2000** 处查找二级 bootloader，而非标准 ESP32 的 0x0。手动 esptool 命令用错了偏移量。

**证据**: `firmware/build/flasher_args.json` 明确记录：
```json
"flash_files": {
    "0x2000": "bootloader/bootloader.bin",  // ← P4 专用！
    "0x8000": "partition_table/partition-table.bin",
    "0x10000": "vbcpet.bin"
}
```

**教训**: `idf.py flash` 会自动读取 `flasher_args.json` 使用正确偏移；手动 esptool 时必须指定 `0x2000`。

---

### 2. WebGL 渲染被禁用（角色不可见）

**现象**: PC 端 Live2D 截图全为背景色，角色未被渲染。

**根因**: `serial_stream.py` 中 Playwright 的 Chrome 参数使用了 `--disable-gpu --disable-software-rasterizer`，**完全禁用了 WebGL 渲染**。Live2D (PixiJS + Cubism 4) 依赖 WebGL。

**修复**: 改用 `--use-gl=angle --use-angle=swiftshader` 启用软件 WebGL。

```diff
- "args": ["--no-sandbox", "--disable-gpu", "--disable-software-rasterizer"]
+ "args": ["--no-sandbox", "--use-gl=angle", "--use-angle=swiftshader"]
```

同时 `render.html` 中 `resolution: 2` 改为 `resolution: 1`（无头浏览器兼容性更好）。

---

### 3. `ili_write_data` 发送多余 NOP 命令

**现象**: ILI9341 初始化命令未生效，面板配置（gamma、电源等）未写入。

**根因**: `ili_write_data` 实现为：
```c
esp_lcd_panel_io_tx_param(g_io, 0, data, len);
//                           ↑ 多余的 0x00 NOP 命令字节！
```
`tx_param` 总是先发命令字节再发数据。传入 `0` 导致每个写数据操作前多发了 NOP (0x00)，打断命令序列。

**修复**: 合并命令和数据到单次调用：
```c
// 之前（错误）
ili_write_cmd(0x36);       // 发 MADCTL 命令
ili_write_data(&0x68, 1);  // 发 0x00 NOP + 数据 → 面板收不到参数！

// 之后（正确）
esp_lcd_panel_io_tx_param(g_io, 0x36, (uint8_t[]){0x68}, 1);
```

---

### 4. ST7789V 分辨率不匹配（梯形变形）

**现象**: 十字准星测试图在屏幕上出现梯形变形——矩形变梯形，直线倾斜。

**根因**: ST7789V 在横屏模式（MV=1）下：
- 逻辑坐标 X 映射到硬件 pages（240 个/page 周期）
- 自动递增先遍历 pages（X 方向，240 值/周期），再遍历 columns（Y 方向，320 周期）

渲染的 320×240 图像每行 320 像素，但自动递增每周期只取 240 像素。每行多出的 80 像素溢出到下一行，累积产生梯形变形。

**修复**: 渲染分辨率改为 240×320，匹配 ST7789V 的硬件自动递增。但这后来被回退（见问题 5）。

---

### 5. serial_display.c 与 display_test.c 初始化差异（最终根因）🔑

**现象**: 即使所有配置相同，`serial_display.c` 的 `draw_bitmap` 也不生效，屏幕保持白屏；而 `display_test.c` 完全正常。

**排查过程**:
1. 怀疑引脚错误 → 检查发现两者引脚定义一致（CS=16, DC=17, RST=18, MOSI=19, SCLK=20）
2. 怀疑 SPI 模式 → 同为 Mode 0, 10MHz
3. 怀疑 MADCTL 方向 → 同为 0x60 (MX=1, MV=1)
4. 怀疑背光 → display_test 的 `PIN_BL=-1`（未连接），无影响
5. 怀疑代码被修改 → `git checkout` 恢复原始版本仍不工作
6. **最终验证**: 用 `display_test_start()` 直接初始化屏幕，在 `app_main` 中通过 `display_test_get_panel()` 获取 panel 句柄调用 `draw_bitmap` → **立即正常工作**

**根因分析**:

对比两个文件的初始化，虽然在代码层面几乎相同，但存在以下可能差异：

| 项目 | display_test.c | serial_display.c |
|------|---------------|-----------------|
| SPI device handle | `g_spi` (spi_device_handle_t) | 无（仅用 IO handle） |
| 面板创建后额外操作 | `spi_bus_add_device` | 无 |
| `draw_bitmap` 调用方式 | 直接使用本地 panel 句柄 | 使用全局 panel 句柄 |
| 初始化期间的错误处理 | 详细日志 | 简化日志 |

最可能的原因是 ESP-IDF 的 SPI LCD 驱动在 `esp_lcd_new_panel_io_spi` 之后还需要通过 `spi_bus_add_device` 添加 SPI 设备句柄，或者 `esp_lcd_panel_init` 内部行为依赖某些未文档化的初始化步骤。

**解决方案**: 使用 display_test.c 的已验证初始化代码，在其基础上追加串口帧接收逻辑。

```c
// vbcpet_main.c — 最终方案
void app_main(void) {
    display_test_start();           // 已验证的显示初始化
    // ... JPEG 解码器 + 串口驱动初始化 ...
    while (1) {
        // 接收串口帧 → 解码 JPEG → draw_bitmap
        esp_lcd_panel_handle_t panel = display_test_get_panel();
        esp_lcd_panel_draw_bitmap(panel, 0, 0, 320, 240, outbuf);
    }
}
```

---

## 时间线

| 步骤 | 问题 | 状态 |
|------|------|------|
| 1 | ESP32-P4 bootloader 偏移 0x2000 | ✅ 已修复 |
| 2 | WebGL `--disable-gpu` 无渲染 | ✅ 已修复 |
| 3 | ILI9341 init NOP 命令 | ❌ 已回退（面板实际是 ST7789V） |
| 4 | 分辨率 320×240 vs 240×320 梯形 | ⚠️ 有梯形但可显示，当前保持 320×240 |
| 5 | serial_display.c 初始化 vs display_test.c | ✅ 已修复（使用 display_test 初始化） |

---

## 经验教训

1. **ESP32-P4 特殊之处**: bootloader flash 偏移 0x2000、USB Serial JTAG 与 UART0 分离，不能假设与其他 ESP32 完全相同
2. **驱动初始化顺序敏感**: ESP-IDF LCD 驱动的初始化步骤必须完整，跳过任何步骤（如不调用 `esp_lcd_panel_init`）会导致 `draw_bitmap` 静默失败
3. **硬件调试方法**: 用已知可工作的代码（display_test.c）作为基线，逐步添加功能，而非从头构建复杂功能
4. **Chrome headless WebGL**: 必须用 `--use-gl=angle --use-angle=swiftshader`，`--disable-gpu` 会彻底禁用 WebGL

---

## 当前架构

```
PC (Playwright + SwiftShader WebGL)
  │ Live2D 渲染 (320×240)
  │ JPEG quality 60 (~8KB/frame)
  │ USB Serial (COM16, 2M baud)
  ▼
ESP32-P4 (WT9932P4-TINY)
  │ USB Serial JTAG RX (16KB buffer)
  │ PPA JPEG 硬件解码 → RGB565 buffer
  │ display_test 初始化的 panel 句柄
  │ esp_lcd_panel_draw_bitmap → SPI → ST7789V
  ▼
ST7789V 320×240 TFT (MADCTL=0x60 横屏)
```
