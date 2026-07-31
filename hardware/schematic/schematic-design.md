# 原理图设计文档 — vbcpet 桌宠主板

> 版本: v0.4 | 日期: 2026-07-30 | 主控: ESP32-P4C6-Core

## Sheet 规划

| Sheet | 内容 |
|-------|------|
| 01 电源 | USB-C + CH224K(可选) + MP2315 Buck + MT3608 背光 |
| 02 主控 | P4C6-Core + Strapping + USB OTG + I2C/I2S/GPIO |
| 03 显示 | DSI FPC 30pin + 触摸 3.3V 直连 + 背光 LED |
| 04 音频 | MAX98357A + ZTS6672 x2 + 喇叭 (全数字 I2S) |
| 05 I/O | 4 凯华轴座 + SK6812 3215 ×4 + NT3H2111 NFC |

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

### CH224K PD (可选)

| 位号 | 元件 | 封装 |
|------|------|------|
| U2 | CH224K | SOT-23-6 |
| R1,R2 | 5.1kohm | 0603 |
| C3 | 1uF | 0603 |

Pin 1=VBUS, 2=GND, 3=CC1, 4=CC2, 5=+3V3(请求9V), 6=NC
> 可去掉, 功耗~4.2W << USB-C 5V/15W

### MP2315 Buck 3.3V

| 位号 | 元件 | 封装 |
|------|------|------|
| U7 | MP2315 | SOT-23-8 |
| L1 | 4.7uH 功率电感 | CD54 |
| C4 | 10uF | 0805 |
| C5 | 22uF | 0805 |
| C6 | 100nF | 0603 |
| R_FBH | 100kohm | 0603 |
| R_FBL | 22kohm | 0603 |

EN=VBUS, VIN=VBUS(4.5-24V), SW->L1->+3V3, FB->R_FBH->+3V3 + R_FBL->GND
Vout=0.6*(1+100k/22k)=3.33V

### MT3608 背光

| 位号 | 元件 | 封装 |
|------|------|------|
| U9 | MT3608 | TSOT-23-6 |
| L2 | 10uH 功率电感 | CD54 |
| D2 | SS14 | SMA |
| C9 | 1uF | 0805 |
| R5 | 5.1ohm | 0603 |

SW->L2->+3V3, VIN=+3V3, EN=GPIO48(BL_PWM), FB->R5->GND, OV->D2->LED_A
LED_A->FPC 1-2, FPC 4-5(LEDK)->R5->GND

---

## Sheet 02: P4C6-Core

| 位号 | 元件 | 封装 |
|------|------|------|
| M1 | ESP32-P4C6-Core | 88pin 22x4 邮票孔 1.27mm |

### 电源引脚

85,86=+3V3(+去耦100nF+10uF), 84=+3V3(RTC), 87=悬空(可加RST按键->GND), 66=悬空(LDO_OUT)
GND: 1,3,15,33,40,47,52,83

### Strapping

GPIO35(63)=悬空, GPIO37(64)=UART0_TXD, GPIO38(65)=UART0_RXD, GPIO34(57)=悬空, GPIO36(60)=悬空

### USB

48=USB_DM->USB1_D-, 49=USB_DP->USB1_D+

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
| 1-2 | LED_A |
| 4-5 | LEDK->R5->GND |
| 11,12 | MIPI_CP/CN |
| 14,15 | MIPI_D1P/N |
| 17,18 | MIPI_D0P/N |
| 21 | LCD_RST |
| 22-23 | +3V3 |
| 25 | I2C_SCL (3.3V直连) |
| 26 | I2C_SDA (3.3V直连) |
| 27 | TP_RST (GPIO9, 3.3V直连) |
| 28 | TP_INT (GPIO10, 3.3V直连) |
| 30 | +3V3 (TP_VDD) |
| 3,6,8,9,20,24 | NC |
| 7,10,13,16,19,29 | GND |

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
| 2 GAIN | GND (3dB) |
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

VCC=+3V3, GND, SCL/SDA=I2C总线, FD=NFC_FD(GPIO11), VOUT=NC, LA/LB=PCB线圈天线

NFC天线: C13,C14(调试), C15,C16(22pF), R12,R13(2.2Ω)

---

## 附录

### I2C地址
NT3H2111=0x55, 触摸=待确认

### BOM
共35行, 见 bom-lcsc.csv。自建封装: P4C6-Core(88pin 22x4), FPC 30pin, IPEX1, 凯华轴座。
