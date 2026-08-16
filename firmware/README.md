# firmware — ESP32-P4 固件

> 基于 **ESP-IDF v5.5.3**, target `esp32p4`。
> 主线: **串口 JPEG 推流管线**(Live2D 截图 → USB 串口 → P4 PPA 硬解 → 屏),已在开发板跑通;
> 最终板 (ESP32-P4C6-Core, MIPI-DSI 480×800) 固件已就绪, bring-up 进行中 (08-17): 模组 boot + 固件运行已确认; DSI 初始化在屏未接时卡死 → 新增 `CONFIG_VBCPET_SKIP_DISPLAY` 跳过显示先测外设。

## 当前状态 (2026-08-11)

| 模块 | 状态 | 说明 |
|------|------|------|
| 显示后端抽象 | ✅ | `display_backend.h`, Kconfig 选择 SPI/DSI |
| SPI 后端 (开发板) | ✅ | ST7789V 240×320, WT9932P4-TINY 已验证 |
| **DSI 后端 (最终板)** | ⚠️ 卡死 | MIPI-DSI 2-lane ST7102 480×800, `esp_lcd_st7102` 组件; 屏未接时 `esp_lcd_panel_init` 忙等卡死 (MIPI_DSI_HOST 0x500A0000)。加 `CONFIG_VBCPET_SKIP_DISPLAY` 跳过 (bring-up 用) |
| JPEG 硬解 | ✅ | PPA 解码, 单缓冲 (解码→文字→flush) |
| 中文渲染 | ✅ | 7548 CJK 字形, UTF-8, 透明背景 |
| 传输 | ✅ | USB Serial JTAG, RX 32KB (满分辨率帧缓冲) |
| **外设驱动** | ✅ 编译验证 | 按键/SK6812/触摸/音频/MPU-6500/SD/舵机/NFC/USB-host (最终板) |
| **bring-up 自检** | ✅ | UART0 控制台 `test <name>` / `test all` / `brightness` |
| WebRTC 接收链路 | ⬜ 后台 | 骨架 + 清单, 见 [`../docs/webrtc-bringup.md`](../docs/webrtc-bringup.md) |

## 构建

两种板型配置 (独立 build 目录, sdkconfig 互不干扰):

| 目标 | 命令 (Git Bash) | bat |
|------|------|------|
| 最终板 (DSI 480×800) | `python _idf.py build` | `_build.bat` |
| 开发板 (SPI 240×320) | `python _idf.py -B build_dev -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.devboard" -D SDKCONFIG="build_dev/sdkconfig" build` | `_build_dev.bat` |

> `_idf.py` 是包装器: 剥掉 MSYSTEM (Git Bash 下 idf.py 会跳过 main) + 设置工具链 env。
> 若在原生 IDF CMD 终端, 直接 `idf.py build` 即可。

### 最终板 bring-up

板到后: 烧录 → 串口监视器 (UART0 console, 波特率 115200 或 esptool 自动) →
UART0 控制台命令:

```
help                       # 命令帮助
list                       # 可用自检
test all                   # 全部外设自检 (逐个跑)
test buttons / leds / touch / audio / imu / sd / servo / nfc / usb
brightness 80              # 背光 0-100
```

自检设计: 每个外设驱动 `xxx_self_test()` 打印关键值 + PASS/FAIL,
逐项核对硬件焊接/接线。上电时序见 `main/bringup.c`。

## 结构

```
firmware/
├── _idf.py                    # idf.py 包装器 (MSYSTEM workaround)
├── _build.bat / _build_dev.bat
├── sdkconfig.defaults         # 最终板 (DSI + PSRAM)
├── sdkconfig.defaults.devboard
└── main/
    ├── vbcpet_main.c          # 主循环: 传输→demux→JPEG+文字→flush
    ├── transport_usb.c        # USB Serial JTAG 传输
    ├── channel_demux.c        # FRAME/CTRL/raw 分流 + 行超时
    ├── display_pipeline.c     # PPA JPEG 解码 + 单缓冲 (PSRAM)
    ├── display_backend.h      # 后端抽象 (W/H/格式 Kconfig 化)
    ├── display_pipeline_spi.c # ST7789V 后端
    ├── display_pipeline_dsi.c # MIPI-DSI ST7102 后端 + 背光
    ├── text_render.c          # CJK 渲染
    ├── protocol.c             # CharacterEvent 协议 (cJSON)
    ├── interaction_engine.c   # CTRL 分发表
    ├── board_config.h         # ★ 最终板引脚唯一权威 (bring-up 调线只改这里)
    ├── bringup.c              # 外设初始化 + UART0 控制台
    ├── peripherals/           # 外设驱动 + 自检
    │   ├── i2c_bus.c          # 共享 I2C (触摸/NFC/MPU/OV5640)
    │   ├── buttons.c / sk6812.c / touch.c / audio.c
    │   ├── mpu6500.c / sd_card.c / servo.c / nfc.c / usb_host.c
    │   └── self_test.c        # 自检注册表
    └── webrtc_skeleton.c      # WebRTC 参考骨架 (未编译)
```

## 打样疑点 (板到后优先核对)

见 [`../hardware/README.md`](../hardware/README.md):
- ⚠️ **D1 (USBLC6) pin5=VBUS_IN** 应为 +5V — 电池模式 USB-A 口会瘫痪 (bring-up 测 USB-A 电池模式)
- ⚠️ **USB_DP/DM 线宽 7.3/10mil** 应为 9.5mil (90Ω)
- ST7102 MIPI lane 顺序交叉 (模组 vs FPC) — 实板对线, 必要时 ST7102 寄存器调

## 参考

- [ESP32-P4 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf)
- [`../docs/webrtc-bringup.md`](../docs/webrtc-bringup.md) — WebRTC 板到后验证
- [`../hardware/final-pinmap.md`](../hardware/final-pinmap.md) — 引脚分配
