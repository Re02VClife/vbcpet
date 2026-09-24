# 原理图设计文档 — vbcpet 桌宠主板

> ⚠️ **已过时 (2026-08-11)**: 本文为基础 5 页设计的**早期记录**。设计已演进并打样，**位号已重排**（如 U7/U9/U12/U16 等已变更）、供电架构已加入 IP5306 电池（VBUS_IN/+5V/BAT+）、FB 分压已改为 40.2k/12.7k。
> **当前网表为准**：嘉立创 EDA 工程 `../../JLC/ProPrj_桌面开发助手_2026-08-11.epro2` + [expansion-modifications.md](expansion-modifications.md) + [../power-tree.md](../power-tree.md)。

> 历史版本: v0.7 | 日期: 2026-08-03 | 主控: ESP32-P4C6-Core
> 外设拓展 (电池/视觉回归/机械臂) 的改动方案: [expansion-modifications.md](expansion-modifications.md) — 本文件为基础 5 页设计。

## Sheet 规划

| Sheet | 内容 |
|-------|------|
| 01 电源 | USB-C + MP2315 Buck + MT3608 背光 (CH224K 已砍) |
| 02 主控 | P4C6-Core + Strapping + USB OTG + I2C/I2S/GPIO |
| 03 显示 | DSI FPC 30pin + 触摸 3.3V 直连 + 背光 LED |
| 04 音频 | MAX98357A + ZTS6672 x2 + 喇叭 (全数字 I2S) |
| 05 I/O | 4 凯华轴座 + SK6812 3215 ×4 + NT3H2111 NFC + USB 外设口 |

### 全局网络

| 网络 | 电压 | 用途 |
|------|------|------|
| `VBUS` | 5V (9V) | USB-C 输入 |
| `+3V3` | 3.3V | MP2315 Buck 输出, 主供电 |
| `LED_A` | 12.2V/40mA | 屏背光 |
| `GND` | 0V | 公共地 |

### 电源拓扑

```
USB-C 5V (CH224K 可选 9V)
  ├─ TVS D1 + 0ohm F1
  ├─ MP2315 Buck -> +3V3 @2A
  │   └─ P4C6-Core / 显示屏 / 触摸 / ZTS6672 / NT3H2111 / MT3608 VIN
  ├─ VBUS -> MAX98357A VDD (5V) + SK6812 ×4 VDD (5V 按键背光)
  └─ MT3608 Boost -> LED_A (12.2V/40mA) -> 屏背光
```

---

## Sheet 01: 电源

### USB-C + 保护

| 位号 | 元件 | 封装 |
|------|------|------|
| USB1 | USB-C 16pin | SMT |
| F1 | 0ohm | 0805 |
| D1 | TVS 5V | SOD-523 |
| C1,C2 | 10uF | 0805 |

USB1: VBUS(A4/9,B4/9)经F1, GND(A1/12,B1/12), D+(A6,B6)->P4_DP, D-(A7,B7)->P4_DM, CC1/2->CH224K

### CH224K PD — 已砍 (2026-08-02 决策)

> 纯 USB 5V, 不请求 9V。CC1/CC2 用 5.1kΩ 下拉到 GND (网表 R2/R3 已实现)。
> 原方案: 请求 9V, 5V 回退。功耗 ~4.2W << USB-C 5V/15W, 无需保留。

### MP2315 Buck 3.3V — ✅ 已确认 (2026-08-02)

| 位号 | 元件 | 封装 |
|------|------|------|
| U7 | MP2315 | SOT-23-8 |
| L1 | 4.7uH 功率电感 | CD54 |
| C4 | 10uF | 0805 |
| C5 | 22uF | 0805 |
| C6 | 100nF | 0603 |
| R_FBH (R6) | 40.2kohm 1% | 0603 |
| R_FBL (R11) | 12.7kohm 1% | 0603 |
| C_VCC | 0.1uF (新增) | 0603 |
| C_BST | 100nF (新增) | 0603 |
| R_BST | 20ohm (新增) | 0603 |

EN=VBUS, VIN=VBUS, SW->L1->+3V3
FB->R_FBH(40.2k)->+3V3 + R_FBL(12.7k)->GND
VCC->C_VCC(0.1uF)->GND;  BST->100nF + 20Ω(串联)->SW
Vout=0.8*(1+40.2k/12.7k)=3.33V
> FB 基准=0.8V(非 0.6V): 原 100k/22k 会输出 4.44V, 2026-08-02 修正

### MT3608 背光

| 位号 | 元件 | 封装 |
|------|------|------|
| U9 | MT3608 | TSOT-23-6 |
| L2 | 10uH 功率电感 | CD54 |
| D2 | SS14 | SMA |
| C9 | 1uF | 0805 |
| R5 | 15ohm | 0603 |

引脚(SOT-23-6): 1=SW, 2=GND, 3=FB, 4=EN, 5=IN, 6=NC
VIN=+3V3, EN=GPIO48(BL_PWM)
SW->L2(10uH)->D2(SS14)阳极, D2阴极->LED_A -> FPC 1-2 (屏内 4S2P LED 串)
FPC 4-5(LEDK) -> R5(15Ω) -> GND, FB 接 R5 与 LEDK 之间节点

恒流: FB 基准=0.6V, I=0.6V/15Ω=40mA
> 屏手册 (YDP430BT009-V1 p8) 确认: 8 LED 4串2并, IF=20mA×2=40mA, Vf=10.8~13.6V (typ 12.2V)
> R5 原 5.1Ω(对应 117mA)错误, 2026-08-02 修正为 15Ω

---

## Sheet 02: P4C6-Core

| 位号 | 元件 | 封装 |
|------|------|------|
| M1 | ESP32-P4C6-Core | 88pin 22x4 邮票孔 1.27mm |

### 电源引脚

85,86=+3V3(+去耦100nF+10uF), 84=+3V3(RTC), 87=悬空(可加RST按键->GND), 66=悬空(LDO_OUT)
GND: 1,3,15,33,40,47,52,83

### Strapping

GPIO35(62)=悬空(内部上拉,默认1), GPIO36(63)=悬空(默认下拉), GPIO37(64)=UART0_TXD, GPIO38(65)=UART0_RXD, GPIO34(61)=悬空 (引脚号按模组手册 ESP32-P4C6-Core 使用指南 p9 核对)

### USB

48=USB_DM->USB1_D-, 49=USB_DP->USB1_D+ (USB-C: 供电 + HS/调试)
GPIO27->USB-A D+ (外设口), GPIO26->USB-A D- (FS OTG, 独立于调试口)

### MIPI-DSI

34=DSI_D1P, 35=DSI_D1N, 36=DSI_CLKN, 37=DSI_CLKP, 38=DSI_D0P, 39=DSI_D0N
(差分100ohm, 等长0.5mm, 包地)

### I2C0

GPIO8(23)=I2C_SDA, GPIO21(30)=I2C_SCL
R6=R7=4.7kohm到+3V3
挂载: NT3H2111(0x55) + 触摸(3.3V直连)

### I2S0 (无MCLK)

GPIO22(31)=I2S_BCLK -> MAX98357A+ZTS6672x2
GPIO7(22)=I2S_WS -> MAX98357A LRCLK + ZTS6672x2 LRC
GPIO23(32)=I2S_DOUT -> MAX98357A DIN
GPIO6(21)=I2S_DIN <- ZTS6672x2 DATA(共用)
GPIO5(20)=释放(无需MCLK)

### GPIO

| 引脚 | 网络 | 去向 |
|------|------|------|
| GPIO20(29) | LCD_RST | 屏复位 |
| GPIO48(76) | BL_PWM | 背光PWM |
| GPIO9(24) | TP_RST | 触摸复位 3.3V直连 |
| GPIO10(25) | TP_INT | 触摸中断 3.3V直连 |
| GPIO46(74) | PA_EN | MAX98357A SD |
| GPIO11(26) | NFC_FD | NT3H2111场检测 |
| GPIO1-4(16-19) | BTN_* | 4按键 |
| GPIO51(79) | LED_DIN | SK6812 ×4 级联数据 (33Ω 匹配, 5V VBUS 供电) |

### C6

2=LAN_OUT->IPEX1天线, 4/5=C6_U0RXD/TXD(测试点)

---

## Sheet 03: 显示

### FPC 30pin

| 位号 | 元件 | 封装 |
|------|------|------|
| 4.3FPC30PIN | FPC 0.5mm 30P | FPC_0.5mm_15x2 |

| Pin | 网络 |
|-----|------|
| 29,30 | LED_A |
| 26,27 | LEDK->R5->GND |
| 13,14 | MIPI_D0N/P |
| 16,17 | MIPI_D1N/P |
| 19,20 | MIPI_CN/CP |
| 10 | LCD_RST |
| 1,8,9 | +3V3 (VDD, 手册 3.0~3.6V typ 3.3V 确认) |
| 5 | I2C_SDA (3.3V直连; 触摸 I2C 标称 1.8V 电平) |
| 6 | I2C_SCL (3.3V直连; 同上) |
| 4 | TP_RST (GPIO9, 3.3V直连) |
| 3 | TP_INT (GPIO10, 3.3V直连) |
| 7,11,22,23,25,28 | NC |
| 2,12,15,18,21,24 | GND |

> 2026-08-05 同步: 上表按当前网表 (08-05) 对实物屏重新对线后的引脚表; 08-03 旧引脚表 (LED 1/2/4/5, CLK 11/12, D1 14/15, D0 17/18, LCD_RST 21, 3V3 22/23, I2C 25/26, TP 27/28) 已废弃。

---

## Sheet 04: 音频

```
         BCLK/WS ─┬─ MAX98357A
                  └─ ZTS6672 x2
P4 I2S0 ─ DOUT ───→ MAX98357A DIN
        ← DIN ───── ZTS6672 x2 DATA (共用, SELECT脚分左右)
```

### MAX98357A (CH98357)

| 位号 | 元件 | 封装 |
|------|------|------|
| U1 | CH98357 | TQFN-16 |

| Pin | 连接 |
|-----|------|
| 1 DIN | I2S_DOUT |
| 2 GAIN | GND (12dB) |
| 3,11,15 | GND |
| 4 SD_MODE# | PA_EN (HIGH=开) |
| 5,6,12,13 | NC |
| 7,8 VDD | VBUS +10uF+100nF |
| 9 OUTP | SPK+ |
| 10 OUTN | SPK- |
| 14 LRCLK | I2S_WS |
| 16 BCLK | I2S_BCLK |
| 17 EP | GND |

> 免滤波D类, 直连喇叭
> GAIN: GND=12dB (原标 3dB 错误; 3dB 需 VDD 经 100kΩ, 15dB 需 GND 经 100kΩ)

### ZTS6672 x2

| 位号 | 元件 | 封装 |
|------|------|------|
| MK1 | ZTS6672 (CH1语音) | 3.5x2.65mm |
| MK2 | ZTS6672 (CH2 AEC) | 3.5x2.65mm |

| Pin | MK1 | MK2 |
|-----|-----|-----|
| 1 LRC | I2S_WS | I2S_WS |
| 2 SELECT | GND | +3V3 |
| 3 GND | GND | GND |
| 4 BCLK | I2S_BCLK | I2S_BCLK |
| 5 VDD | +3V3 | +3V3 |
| 6 DATA | I2S_DIN | I2S_DIN(共用) |
| 7 CONFIG | +3V3 | +3V3 |

### 喇叭

J3 PH-2pin -> SPK1 8ohm/1-2W

---

## Sheet 05: I/O

### 凯华热插拔轴座 ×4 + SK6812 3215 反贴灯 ×4

每个轴座 6pin: VDD(1) / DOUT(2) / GND(3) / DIN(4) / K0(5) / K1(6)

按键: K0→GPIO1-4, K1→GND, 各 10kΩ 上拉到 +3V3

LED 级联 (5V VBUS 供电):
```
GPIO51 ──[33Ω]── LED1_DIN
LED1_DOUT ────── LED2_DIN
LED2_DOUT ────── LED3_DIN
LED3_DOUT ────── LED4_DIN  (DOUT=NC)
VDD 全接 VBUS(5V) + 100µF 总去耦 + 每灯 100nF 旁路
```

> SK6812 3215 反贴灯: 焊接在 PCB 背面, 光从 PCB 开孔穿过轴体导光柱。
> 限流电阻: 无需 (SK6812 内置恒流驱动)。仅需数据线 33Ω 阻抗匹配。
> 电平: 3.3V GPIO 直驱 5V LED 在实践中可行, 保守可串 1N4148 降 VDD 至 ~4.3V。

### NT3H2111 NFC

| 位号 | 元件 | 封装 |
|------|------|------|
| U16 | NT3H2111 | SO-8 / TSSOP-8 |

VCC=+3V3, GND, SCL/SDA=I2C总线, FD=NFC_FD(GPIO11) + 4.7k上拉→+3V3(开漏), VOUT=NC, LA/LB=PCB线圈天线

NFC天线: C13,C14(调试), C15,C16(22pF), R12,R13(2.2Ω)

### USB 外设口 (2026-08-03 新增)

| 位号 | 元件 | 封装 |
|------|------|------|
| J2 | USB-A 母座 (USB 2.0) | SMT/THT |
| F_USB | PPTC 500mA | 0805 |
| D_USB | USBLC6-2SC6 | SOT-23-6L |
| C_USB_BULK | 100µF | 1206 |

```
D+ = GPIO27, D- = GPIO26 (FS OTG, 独立于 USB-C 调试口)
VBUS = 板载 5V → F_USB PPTC 500mA → J2 VBUS
ESD  D_USB 跨 D+/D-/VBUS; C_USB_BULK 100µF VBUS 去耦
外壳 → GND
```

> 12Mbps FS: 机械臂(CDC)/电子设备/HID/手机低速互连 ✅; UVC 摄像头仅低分辨率。
> 详见 [expansion.md](../expansion.md) 第 7 节。

---

## 附录

### I2C地址
NT3H2111=0x55, 触摸=待确认

### BOM
共39行, 见 bom-lcsc.csv。自建封装: P4C6-Core(88pin 22x4), FPC 30pin, IPEX1, 凯华轴座, USB-A。
