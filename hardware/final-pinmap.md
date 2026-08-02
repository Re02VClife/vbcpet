# 最终板引脚分配 — ESP32-P4C6-Core

> 模组: 鱼鹰光电 ESP32-P4C6-Core, 88pin 邮票孔 (22×4)
> 显示: YDP430BT009-V1 (ST7102 MIPI-DSI + 触摸 3.3V)
> 更新: 2026-07-30

## 模组约束

- **GPIO14-19 未引出** (内部 PSRAM/Flash 占用)
- **GPIO34-38 是 Strapping 引脚**, 避免做普通 I/O
  - GPIO35: Boot=SPI (内部上拉, 默认=1)
  - GPIO37: UART0_TXD → 调试串口
  - GPIO38: UART0_RXD → 调试串口
  - GPIO34/36: 悬空 (默认下拉)

## 完整引脚分配

### MIPI-DSI 显示 (专用 PHY)

| 模组 Pin | 信号 | 去向 |
|----------|------|------|
| 34 | DSI_D1P | → 屏 FPC-14 |
| 35 | DSI_D1N | → 屏 FPC-15 |
| 36 | DSI_CLKN | → 屏 FPC-12 |
| 37 | DSI_CLKP | → 屏 FPC-11 |
| 38 | DSI_D0P | → 屏 FPC-17 |
| 39 | DSI_D0N | → 屏 FPC-18 |

### I²C0 共享总线

| 模组 Pin | GPIO | 信号 | 去向 |
|----------|------|------|------|
| 23 | **GPIO8** | I2C_SDA | → NT3H2111(0x55) + 触摸 TP_SDA |
| 30 | **GPIO21** | I2C_SCL | → NT3H2111(0x55) + 触摸 TP_SCL |

> 上拉: R6=R7=4.7kΩ 到 +3V3

### I²S0 音频 (全双工, 无 MCLK)

| 模组 Pin | GPIO | 信号 | 方向 | 去向 |
|----------|------|------|------|------|
| 31 | **GPIO22** | I2S_BCLK | OUT | → MAX98357A BCLK + ZTS6672×2 BCLK |
| 22 | **GPIO7** | I2S_WS | OUT | → MAX98357A LRCLK + ZTS6672×2 LRC |
| 32 | **GPIO23** | I2S_DOUT | OUT | → MAX98357A DIN |
| 21 | **GPIO6** | I2S_DIN | IN | ← ZTS6672×2 DATA (两麦共用, L/R 脚分声道) |
| 20 | **GPIO5** | (释放) | — | ZTS6672/MAX98357A 无需 MCLK |

### 显示控制

| 模组 Pin | GPIO | 信号 | 去向 |
|----------|------|------|------|
| 29 | **GPIO20** | LCD_RST | → 屏 FPC-21 |
| 76 | **GPIO48** | BL_PWM | → MT3608 EN (背光 PWM) |

### 触摸 (3.3V 直连)

| 模组 Pin | GPIO | 信号 | 去向 |
|----------|------|------|------|
| (I²C 总线) | — | TP_SCL | → 屏 FPC-25 |
| (I²C 总线) | — | TP_SDA | → 屏 FPC-26 |
| 24 | **GPIO9** | TP_RST | → 屏 FPC-27 |
| 25 | **GPIO10** | TP_INT | ← 屏 FPC-28 |

### 音频控制

| 模组 Pin | GPIO | 信号 | 去向 |
|----------|------|------|------|
| 74 | **GPIO46** | PA_EN | → MAX98357A SD_MODE (HIGH=开) |

### NFC (NT3H2111)

| 模组 Pin | GPIO | 信号 | 去向 |
|----------|------|------|------|
| (I²C 总线) | — | SDA/SCL | I²C 0x55 |
| 26 | **GPIO11** | NFC_FD | ← NT3H2111 FD (场检测) |

### 按键 ×4 (凯华热插拔轴座, Cherry MX 兼容)

| 轴座 Pin | 连接 |
|----------|------|
| K0 | → GPIO (GPIO1-4) |
| K1 | → GND |

| 模组 Pin | GPIO | 功能 |
|----------|------|------|
| 16 | **GPIO1** | BTN_YES (SW1) |
| 17 | **GPIO2** | BTN_NO (SW2) |
| 18 | **GPIO3** | BTN_YES_ALWAYS (SW3) |
| 19 | **GPIO4** | BTN_PANEL (SW4) |

> 各 10kΩ 上拉到 +3V3, 按下 K0-K1 导通 → GPIO 读 LOW

### 按键背光 LED ×4 (SK6812 3215 反贴灯)

| 模组 Pin | GPIO | 功能 |
|----------|------|------|
| 79 | **GPIO51** | LED1_DIN (SK6812 级联, 5V VBUS 供电, 33Ω 匹配) |

> SK6812 mini-E 3215: WS2812B 协议, VDD=VBUS(5V), 内置恒流无需限流电阻。

### 调试串口 (UART0)

| 模组 Pin | GPIO | 信号 |
|----------|------|------|
| 64 | GPIO37 | UART0_TXD |
| 65 | GPIO38 | UART0_RXD |

### USB OTG

| 模组 Pin | 信号 | 去向 |
|----------|------|------|
| 48 | USB_DM | → USB-C D- |
| 49 | USB_DP | → USB-C D+ |

### 电源

| 模组 Pin | 信号 | 说明 |
|----------|------|------|
| 85,86 | ESP_3V3 | 3.3V 输入 (MP2315 Buck 输出) |
| 1,3,15,33,40,47,52,83 | GND | 电源地 |
| 84 | RTC_VBAT | 接 +3V3 |
| 87 | CHIP_PU | 内部 RC 复位, 悬空 (可加按键到 GND) |
| 66 | ESP_LDO_VO4 | 悬空 |

### C6 侧

| 模组 Pin | 信号 | 备注 |
|----------|------|------|
| 2 | LAN_OUT | → IPEX1 天线座 |
| 4,5 | C6_U0RXD/TXD | C6 调试 (预留测试点) |
| 6-14 | C6_IO4-9,13-15 | 闲置扩展 |

### 外设拓展 (2026-08-03) — 详见 [expansion.md](expansion.md)

| 模组 Pin | GPIO | 功能 |
|----------|------|------|
| 41-46 | CSI_* | OV5640 MIPI 2-lane (D0P/N, D1P/N, CLKP/N) |
| 20 | GPIO5 | CAM_MCLK (OV5640 XVCLK 24MHz) |
| 27 | GPIO12 | CAM_RSTB |
| 28 | GPIO13 | CAM_PWDN |
| 50 | GPIO24 | SERVO_YAW (云台水平 PWM) |
| 51 | GPIO25 | SERVO_PITCH (云台俯仰 PWM) |
| 67 | GPIO39 | SD_DAT0 |
| 68 | GPIO40 | SD_DAT1 |
| 69 | GPIO41 | SD_DAT2 |
| 70 | GPIO42 | SD_DAT3 |
| 71 | GPIO43 | SD_CLK |
| 72 | GPIO44 | SD_CMD |
| 73 | GPIO45 | SD_CD (卡检测, 可选) |
| 75 | GPIO47 | MPU_INT (六轴中断) |
| 23/30 | GPIO8/21 | I2C 总线新增: OV5640 (0x3C) + MPU-6500 (0x68) |

---

## 供电架构

```
USB-C VBUS (5V)
  │
  ├─ TVS D1 + 0Ω F1 保护
  │
  ├─ MP2315 Buck → +3V3 @ 2A
  │   ├─ P4C6-Core ESP_3V3 ×2
  │   ├─ 显示屏 VCC (FPC 22-23)
  │   ├─ 触摸 TP_VDD (FPC 30)
  │   ├─ ZTS6672 ×2
  │   ├─ NT3H2111
  │   └─ MT3608 VIN (Boost→背光)
  │
  ├─ VBUS 直供 → MAX98357A VDD (5V) + SK6812 ×4 (5V 按键背光)
  │
  └─ MT3608 Boost → LED_A (12.2V/40mA)
      └─ 屏背光 LEDA/LEDK
```

## I²C 地址

| 设备 | 地址 | 备注 |
|------|------|------|
| NT3H2111 | 0x55 | NFC Tag |
| 触摸 (ST7102 内置) | 待确认 | 3.3V 直连 |

## 与开发板引脚变化

| 功能 | 开发板 (WT9932P4-TINY) | 最终板 (P4C6-Core) |
|------|------------------------|-------------------|
| 显示接口 | SPI IO16-20 | MIPI-DSI 34-39 |
| I2C SCL | IO18 | **IO21** |
| I2S BCLK | IO16 | **IO22** |
| I2S DOUT | IO15 | **IO23** |
| LCD RST | IO18 | **IO20** |
| BL PWM | 无 | **IO48** |
| 按钮 | 无 | IO1-4 |
| LED | IO51 | IO51 |
