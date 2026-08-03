# 模块化设计方案

> 日期: 2026-07-30 | 版本: v1.0

## 原则

优先外购成品模块，无法购买的元件才放主板上。每模块独立可测，故障可换。

## 模块清单

### 外购模块 (4 个)

| 模块 | 型号 | 单价 | 功能 | 接口 |
|------|------|------|------|------|
| 功放 | MAX98357A I2S 模块 | ¥8 | I2S DAC+3W D类 | VIN/GND/I2S/OUT |
| 麦克风 | ZTS6672 I2S 模块 ×2 | ¥5×2 | 数字 MEMS 麦 | VDD/GND/I2S/LR |
| 天线 | 2.4G IPEX1 软天线 | ¥1 | C6 WiFi/BLE | IPEX1 扣接 |
| 按键背光 | SK6812 mini-E 3215 ×4 | ¥2×4 | 反贴 RGB LED | VDD/GND/DIN/DOUT 级联 |

### 主板元件 (必须焊)

| 位号 | 元件 | 封装 | 功能 |
|------|------|------|------|
| M1 | ESP32-P4C6-Core | 88pin 22×4 | 核心模组 (P4+C6+PSRAM+Flash) |
| U7 | MP2315 Buck | SOT-23-8 | VBUS→3.3V 2A 主供电 |
| U9 | MT3608 Boost | SOT-23-6 | 3.3V→12.2V/40mA 背光 |
| L1 | 4.7µH 功率电感 | CD54 | MP2315 Buck 电感 |
| L2 | 10µH 功率电感 | CD54 | MT3608 Boost 电感 |
| D2 | SS14 肖特基 | SMA | Boost 整流 |
| R_FBH/R_FBL | 100k/22k | 0603 | MP2315 反馈分压 |
| R5 | 15Ω | 0603 | MT3608 恒流检测 |
| C4 | 10µF | 0805 | Buck 输入去耦 |
| C5,C6 | 22µF+100nF | 0805+0603 | Buck 输出去耦 |
| C9 | 1µF | 0805 | Boost 输出去耦 |
| 4.3FPC30PIN | FPC 0.5mm 30P 下接 | SMT | 显示屏接口 |
| USB1 | USB-C 16pin | SMT | 供电+USB2.0 OTG |
| F1 | 0Ω | 1206 | USB 跳线(替保险丝) |
| D1 | TVS 5V | SOD-523 | ESD 保护 |
| J2 | USB-A 母座 | SMT/THT | 外设口 (FS OTG, GPIO26/27) |
| F_USB | PPTC 500mA | 0805 | 外设口 VBUS 限流 |
| D_USB | USBLC6-2SC6 | SC70-6 | 外设口 ESD |
| C_USB_BULK | 100µF | 1206 | 外设口 VBUS 去耦 |
| SW1-4 | 凯华热插拔轴座 | MX 14×14mm 兼容 | 4 按键 (Cherry MX 可拔插) |
| RST | 轻触 6×6mm | THT | 硬件复位 |
| LED1-4 | SK6812 mini-E | 3215 ×4 反贴灯 | 按键 RGB 背光 (级联, 5V VBUS) |
| U16 | NT3H2111 | SO-8 / TSSOP-8 | NFC Tag Plus |
| C13,C14 | NFC 匹配(调试) | 0603 | 天线匹配 |
| C15,C16 | 22pF | 0603 | NFC EMC |
| R12,R13 | 2.2Ω | 0603 | NFC 阻尼 |
| R6,R7 | 4.7kΩ ×2 | 0603 | I2C 上拉 |
| R8-R11 | 10kΩ ×4 | 0603 | 按键上拉 |
| C_M | 100nF ×4 | 0603 | 每灯去耦 (紧贴 VDD) |
| R_DATA | 33Ω | 0603 | GPIO51→LED1_DIN 数据线匹配 |
| C_LED_BULK | 100µF | 1206 | LED VDD 总去耦 |

**主板 BOM: 39 行** (含外购模块 5 行)

## 系统框图

```
┌──────────────────────────────────────────────────────┐
│                    USB-C (5V)                        │
│                                                      │
│  ┌── MP2315 Buck ──→ +3V3 ──────────────────┐       │
│  │  └─ MT3608 Boost ──→ LED_A (12.2V/40mA)  │       │
│  │                                           │       │
│  │  ┌─────────────────────────────────────┐  │       │
│  │  │         主板 ESP32-P4C6-Core         │  │       │
│  │                                               │    │
│  │  I2C ────── NT3H2111 + 触摸                  │    │
│  │  I2S ────── MAX98357A 模块 + ZTS6672×2 模块  │    │
│  │  DSI ────── FPC30 → 显示屏                    │    │
│  │  GPIO ───── 4按键(轴座) + SK6812 + RST         │    │
│  │  USB  ───── USB-C D+/D- (HS/调试)             │    │
│  │  USB2 ───── USB-A D+/D- (FS 外设口)           │    │
│  │  IPEX1 ──── 2.4G 天线                         │    │
│  └───────────────────────────────────────────────┘    │
│                                                       │
│  ┌──────────┐  ┌──────────┐                           │
│  │MAX98357A │  │ZTS6672×2 │                           │
│  │ 功放模块  │  │ 麦模块    │                           │
│  └──────────┘  └──────────┘                           │
└──────────────────────────────────────────────────────┘
```

## 接线表

### 电源 (板上)

| MP2315 Buck | 连 |
|------------|-----|
| Pin1 EN | ← VBUS |
| Pin2 VIN | ← VBUS (4.5-24V) |
| Pin3 SW | → L1(4.7µH) → +3V3 |
| Pin4 GND | → GND |
| Pin5 FB | → 100k→+3V3 + 22k→GND |
| Pin6 BST | → 100nF → SW |
| Pin7 SS | NC |
| Pin8 PG | NC |
| C4 10µF | VIN→GND |
| C5 22µF + C6 100nF | +3V3→GND |

| MT3608 Boost | 连 |
|-------------|-----|
| Pin1 SW | → L2(10µH) → +3V3 |
| Pin2 GND | → GND |
| Pin3 FB | → R5(15Ω) → GND |
| Pin4 EN | ← BL_PWM (GPIO48) |
| Pin5 VIN | ← +3V3 |
| D2 SS14 | SW→LED_A |
| C9 1µF | LED_A→GND |
| R5 15Ω | FB恒流 0.6V/0.04A=15Ω |

> LED_A → FPC 1-2, FPC 4-5(LEDK) → R5 → GND

### 音频模块

| MAX98357A 模块 | 连 |
|---------------|-----|
| VIN | ← VBUS (5V) |
| GND | → GND |
| BCLK | ← I2S_BCLK (GPIO22) |
| LRCLK | ← I2S_WS (GPIO7) |
| DIN | ← I2S_DOUT (GPIO23) |
| SD | ← PA_EN (GPIO46) |
| GAIN | → GND (3dB) |
| OUT+/- | → 喇叭座 |

| ZTS6672 MK1 模块 (CH1 语音) | 连 |
|---------------------------|-----|
| VDD | ← +3V3 |
| GND | → GND |
| BCLK | ← I2S_BCLK |
| LRC | ← I2S_WS |
| DOUT | → I2S_DIN (GPIO6) |
| SELECT | → GND (左声道) |

| ZTS6672 MK2 模块 (CH2 AEC) | 连 |
|---------------------------|-----|
| VDD | ← +3V3 |
| GND | → GND |
| BCLK | ← I2S_BCLK |
| LRC | ← I2S_WS |
| DOUT | → I2S_DIN (GPIO6, 与 MK1 共用) |
| SELECT | → +3V3 (右声道) |

### NFC

| NT3H2111 | 连 |
|----------|-----|
| VCC | ← +3V3 |
| GND | → GND |
| SCL | → I2C_SCL (GPIO21) |
| SDA | → I2C_SDA (GPIO8) |
| FD | → NFC_FD (GPIO11) |
| LA/LB | → PCB 线圈天线 (3-4匝 ~25×25mm) |

> NT3H2111 是 NT3H2111 升级版: 密码保护、NDEF 888字节、新版硅片、pin2pin兼容、更便宜。

### USB 外设口 (FS OTG, GPIO26/27)

| USB-A 母座 (J2) | 连 |
|-----------------|-----|
| VBUS | ← VBUS 5V (经 F_USB PPTC 500mA) |
| D+ | ← GPIO27 (USB_DP) |
| D- | ← GPIO26 (USB_DM) |
| GND | → GND (外壳屏蔽) |

> ESD: D_USB (USBLC6-2SC6) 跨 D+/D-/VBUS; C_USB_BULK 100µF VBUS 去耦。
> 与调试口 (USB-C, HS pin48/49) 完全独立, 零冲突。

### 显示

| FPC Pin | 连 |
|---------|-----|
| 1-2 LEDA | ← LED_A (MT3608 OUT+) |
| 4-5 LEDK | → R5→GND |
| 11-18 DSI | ← P4 DSI PHY |
| 21 RSTB | ← LCD_RST (GPIO20) |
| 22-23 VCC | ← +3V3 |
| 25 TP_SCL | ← I2C_SCL |
| 26 TP_SDA | ↔ I2C_SDA |
| 27 TP_RST | ← GPIO9 |
| 28 TP_INT | → GPIO10 |
| 30 TP_VDD | ← +3V3 |

## I2C 地址

| 设备 | 地址 |
|------|------|
| NT3H2111 | 0x55 |
| 触摸 (ST7102) | 待确认 |

## 主板引脚汇总

```
+3V3  ── 排针 ── M1:85,86 + 全板供电
GND   ── 排针 ── M1:1,3,15,33,40,47,52,83 + 全板地

I2C_SDA ── M1:23(GPIO8) ── NT3H2111 + FPC25 + R6(4.7k→+3V3)
I2C_SCL ── M1:30(GPIO21) ── NT3H2111 + FPC26 + R7(4.7k→+3V3)

I2S_BCLK ─ M1:31(GPIO22) ── MAX98357A + ZTS6672×2
I2S_WS ─── M1:22(GPIO7)  ── MAX98357A LRCLK + ZTS6672×2 LRC
I2S_DOUT ─ M1:32(GPIO23) ── MAX98357A DIN
I2S_DIN ── M1:21(GPIO6)  ── ZTS6672×2 DOUT

LCD_RST ── M1:29(GPIO20) ── FPC21
BL_PWM ─── M1:76(GPIO48) ── MT3608 EN
TP_RST ─── M1:24(GPIO9)  ── FPC27
TP_INT ─── M1:25(GPIO10) ── FPC28
PA_EN ──── M1:74(GPIO46) ── MAX98357A SD
NFC_FD ─── M1:26(GPIO11) ── NT3H2111 FD

SW1_K0 ── M1:16(GPIO1) ── 轴座 Pin5, R8(10k→+3V3)
SW1_K1 ── GND ──────────── 轴座 Pin6
SW2_K0 ── M1:17(GPIO2) ── 轴座 Pin5, R9(10k→+3V3)
SW2_K1 ── GND ──────────── 轴座 Pin6
SW3_K0 ── M1:18(GPIO3) ── 轴座 Pin5, R10(10k→+3V3)
SW3_K1 ── GND ──────────── 轴座 Pin6
SW4_K0 ── M1:19(GPIO4) ── 轴座 Pin5, R11(10k→+3V3)
SW4_K1 ── GND ──────────── 轴座 Pin6

LED级联(VBUS 5V): GPIO51─[33Ω]→LED1_DIN, LED1_DOUT→LED2_DIN, LED2_DOUT→LED3_DIN, LED3_DOUT→LED4_DIN, LED4_DOUT=NC
CHIP_PU ── M1:87 ── RST→GND

USB_DM ── M1:48 ── USB1 D-
USB_DP ── M1:49 ── USB1 D+
USB_DM2 ─ M1:53(GPIO26) ── USB-A D- (FS 外设口)
USB_DP2 ─ M1:54(GPIO27) ── USB-A D+ (FS 外设口)
LAN_OUT ─ M1:2 ── IPEX1天线

GPIO5 ─── M1:20 ── 悬空 (无MCLK)
```

## 与项目需求对照

| 需求 | 模块 | 状态 |
|------|------|------|
| PC→P4 JPEG推流 | USB-C OTG | ✅ USB Serial JTAG, 13.8fps 已验证 |
| Live2D角色+表情+心情 | PC端render.html | ✅ 动作队列v3 |
| Claude AI交互 | serial_bridge.py | ✅ v0.2 |
| MIPI-DSI显示 | YDP430BT009-V1 | ✅ 480×800 ST7102 |
| 触摸交互 | FPC直连 | ✅ 3.3V I2C |
| 音频TTS+语音 | MAX98357A + ZTS6672×2 | ✅ 全数字I2S |
| 手机NFC识别 | NT3H2111 | ✅ 开APP + WiFi传数据 |
| WiFi/BLE联网 | P4C6-Core内置C6 | ✅ |
| 4按键工具授权 | SW1-4 | ✅ |
| 状态指示 | SK6812 3215 ×4 | ✅ |
| 单USB-C供电+通信 | USB1直连 | ✅ |

**全部 11 项需求可达成。**

## 与全分立方案对比

| | 全分立 | 模块化 |
|------|--------|--------|
| 主板 BOM 行数 | 30 | 39 |
| 音频调试 | 自画 MAX98357A+麦外围 | 成品模块已验证 |
| 电源调试 | 自画 全部 | 自画 Buck+Boost (常见芯片) |
| 故障更换 | 拆焊 IC | I2S 模块可拔插 |
| 总成本 | ~¥65 | ~¥60 (音频模块溢价,电源自画节约) |
