# 原理图修改方案 — vbcpet 外设拓展

> ✅ **已完成 (2026-08-11)**: 本方案的三项改动（IP5306 电池 / OV5640 摄像头 / 机械臂走 USB-A）已在原理图实装、网表复核通过并随板打样。本文作为**历史过程记录**保留。
> 版本: v1.0 | 日期: 2026-08-04 | 对应 [expansion.md](../expansion.md) v1.3
> 范围: 电池+充电 / 视觉回归 / 机械臂 — 3 项, 含电源拓扑改造。

## 一、改动总览

| # | 改动 | 涉及 Sheet | 新元件 | 新网络 |
|---|------|-----------|--------|--------|
| 1 | 电池 + IP5306 充电/路径/升压 | 01 电源 | U_IP, L3, BAT1, C_BAT | VBUS_IN, BAT+, BAT- |
| 2 | 视觉 OV5640 回归 | 05 I/O | J_CAM (FPC), U_CAM, **LDO1/LDO2, FB1/FB2** | CAM_* 接上 + **+2V8/+1V5** |
| 3 | 机械臂 → USB-A 口 | 05 I/O | **零新增** | 无 (USB-A 已有) |

> **建议顺序**: 先改 1(电源, 牵一发动全身), 再 2 → 3。
> 每改完一板我跑一次 `tools/schematic_review.py` 复查。

---

## 二、改动 1: 电源拓扑 — IP5306 + 电池

### 现状

```
USB-C 5V ──F1── VBUS ──→ MP2315 VIN ──→ +3V3
                     ├─→ MAX98357A VDD
                     ├─→ SK6812 ×4 VDD
                     ├─→ J2 USB-A VBUS (PPTC)
                     └─→ 舵机云台座 (470µF)
```

### 目标

```
USB-C 5V ──F1── VBUS_IN ──→ IP5306 VIN
                             │
          ┌──────────────────┤
          │                 IP5306 (充电 + 电源路径 + 5V 升压)
          │                  SW ←→ L3 (1µH)
          │                  BAT ─ BAT+   电池 (3.7V + 保护板)
          │                       BAT- ─ GND
          │                  LED1-3 ── 电量指示灯 (可选)
          ▼
       5V_SYS (=原 VBUS 网, 由 IP5306 VOUT 供电)
          ├─→ MP2315 VIN / MAX98357A / SK6812 / J2 USB-A / 舵机座
```

- **USB 插入时**: IP5306 直通 5V 到 VOUT, 同时给电池充电。
- **USB 拔掉时**: IP5306 电池升压 5V → VOUT, 无缝切换。
- 全板 **5V 拓扑零改动**, 只是 VBUS 网的来源从 USB-C 换成 IP5306 VOUT。

### 在 EDA 中的具体步骤

1. **改名网络**: USB-C VBUS 引脚(A4/A9/B4/B9)所在网 → `VBUS_IN`。
   原 `VBUS` 网名保留 = 系统 5V 轨(现在由 IP5306 VOUT 供电)。
2. **新增 U_IP — IP5306** (ESOP-8, 需外接 1µH 电感 L3):

   | Pin | 网络 | 说明 |
   |-----|------|------|
   | 1 VIN | VBUS_IN | USB 5V 输入 |
   | 2/3/4 LED1-3 | IP_LED1-3 | 电量指示(可选, 或空) |
   | 5 KEY | (按钮/GPIO/悬空) | 短按开机, 双击关机; 不用则悬空 |
   | 6 BAT | BAT+ | 电池正极 |
   | 7 SW | L3 (1µH) → BAT+ | 外接升压/降压电感 |
   | 8 VOUT | VBUS (=5V 系统轨) | 5V 输出 |
   | PowerPAD | GND | 散热地, 必须接地 |

3. **新增 BAT1** (2pin 电池座): 1→BAT+, 2→GND(电池负)。
   电池**外接保护板**(过放 3.0V / 过充 / 过流), 不在板上画。
4. **新增 L3** (1µH 功率电感, CD32/0603 磁屏蔽), C_BAT (100µF + 22µF + 100nF 去耦) 就近 BAT+。
5. **(可选) 电量**: IP5306 LED1-3 接 3 颗小灯(免软件); 或加 I2C MAX17048 电量计(挂 I2C, 零额外引脚)。
6. **(可选) KEY 接 GPIO**: 用 P4 一个 GPIO 模拟按键做软件开关机(接 GPIO33)。

### IP5306 关键参数

| 参数 | 值 |
|------|----|
| 充电 | 2.1A 同步开关(默认 1A 稳妥) |
| 放电(升压) | 2.4A 同步 boost, 92% |
| VIN | 4.5~5.5V(5V 典型) → USB 5V 直接可用 |
| VOUT | 5.0V(>1A 时带线损补偿 5.15V) |
| 电池 | 3.0~4.4V, 选 **4.2V 截止版本** |
| 待机 | <100µA(空载自动睡眠) |
| 外围 | 仅 1 颗 1µH 电感, 功率管内置 |

> ⚠️ 空载自动睡眠: P4+屏+功放常态 >100mA, 远高于睡眠阈值, 正常使用**不会**误关; 若极端低功耗待机需注意。

---

## 三、改动 2: 视觉 OV5640 回归

> ✅ 2026-08-04 定案 (v2): 模块 **OV5640 MIPI 24pin** (0.5mm FPC, 自动对焦)。取代早期 FYRK3399-30pin 方案。
> **不带板载稳压** — 4 路电源外供, 需新增 LDO1/LDO2。网表里 CAM_PWDN/CAM_RSTB/CAM_MCLK 已是孤立网, 现在接上。

### 在 EDA 中的具体步骤

1. **新增 J_CAM** (24pin 0.5mm FPC 座), **U_CAM** (OV5640 模块)。
2. **新增电源**: LDO1 (ME6211A28, 2.8V 低压差), LDO2 (AMS1117-1.5), FB1/FB2 (磁珠):

   | 元件 | 输入 | 输出 | 去向 |
   |------|------|------|------|
   | LDO1 ME6211A28 | +3V3 | **+2V8** | → DOVDD (pin11) 直供; → FB1 → AVDD (pin4); → FB2 → AF-VCC (pin23); GND/CE=3V3 |
   | LDO2 AMS1117-1.5 | +3V3 | **+1V5** | → DVDD (pin10); TAB(pin4)→+1.5V 散热 |

3. **接线**(2-lane MIPI, 按 24pin pinout):

   | P4 信号 | → J_CAM pin | 模组信号 |
   |---------|------------|---------|
   | CSI_D0P (pin42) | 20 | MDP0 |
   | CSI_D0N (pin41) | 22 | MDN0 |
   | CSI_D1P (pin46) | 12 | MDP1 |
   | CSI_D1N (pin45) | 13 | MDN1 |
   | CSI_CLKP (pin43) | 16 | MCP |
   | CSI_CLKN (pin44) | 18 | MCN |
   | GPIO5 | 14 | MCLK (24MHz) |
   | GPIO8 (I2C_SDA) | 3 | SDA |
   | GPIO21 (I2C_SCL) | 5 | SCL |
   | GPIO12 | 6 | REST (低有效) |
   | GPIO13 | 8 | PWDNB (极性待确认) |
   | +2V8 | 11/4/23 | DOVDD / AVDD / AF-VCC |
   | +1V5 | 10 | DVDD |
   | GND | 2,15,24 | AGND / GND / AF-GND (单点接平面) |
   | STROBE | 1 | 闪光触发 (可选, 暂 NC) |
   | NC | 7,9,17,19,21 | 悬空 |

4. **网络**: 沿用已有 `CAM_MCLK/CAM_RSTB/CAM_PWDN`, MIPI 4 对差分走 FPC, I2C 挂总线(0x3C)。新增 `+2V8` / `+1V5`。
5. 上电时序: RST 保持低直到 rails 稳定, 固件在 esp_cam 初始化时处理。

---

## 四、改动 3: 机械臂 — USB-A 外设口 (2026-08-05 改方案)

> 机械臂 **不再占用 GPIO/UART**, 直接接已有 USB-A 外设口 (FS OTG host, GPIO26/27)。
> 板上 **零新增元件**。SO101 控制板独立供电, P4 只发控制指令。

### 在 EDA 中的具体步骤

1. **无需改动**: USB-A 口 (USB2/F1/D2) 已画好, 直接给机械臂控制板用。
2. **GPIO28/29 保持 SERVO3/SERVO4** (U9/U11 备用舵机), 不改名。
3. **不新增** J_ARM / J_EXT5V / ARM_* 网络。
4. 软件: P4 usb_host + CDC-ACM → SO101 控制板协议 (待确认), 详见 expansion.md 九节。

---

## 五、新增元件汇总 (BOM 增量)

| 位号 | 元件 | 封装 | 说明 |
|------|------|------|------|
| U_IP | IP5306 | ESOP-8 | 充电+电源路径+5V升压 |
| L3 | 1µH 功率电感 | 磁屏蔽小体积 | IP5306 SW |
| BAT1 | 2pin 电池座 | 卧式 | 1S LiPo + 外接保护板 |
| C_BAT | 100µF + 22µF + 100nF | 1206/0805/0603 | 电池侧去耦 |
| J_CAM | 24pin 0.5mm FPC 座 | 翻盖 | 摄像头 |
| U_CAM | OV5640 MIPI 24pin 模块 | — | 外购含镜头, 无板载稳压 |
| LDO1 | ME6211A28 (2.8V/500mA) | SOT-23-5 | 摄像头 +2V8 (DOVDD/AVDD/AF-VCC), 低压差必须 |
| LDO2 | AMS1117-1.5 (3.3→1.5V) | SOT-223 | 摄像头 +1V5 (DVDD), TAB→+1.5V |
| FB1/FB2 | 磁珠 (600Ω@100MHz) | 0603 | AVDD/AF-VCC 隔离 |
| (可选) U_G | MAX17048 | DFN-8 | I2C 电量计 |
| (可选) LED | 3 颗电量灯 | 0603 | IP5306 LED1-3 |

## 六、新增/改名网络汇总

| 网络 | 类型 | 说明 |
|------|------|------|
| VBUS_IN | 改名 | USB-C 5V → IP5306 VIN |
| VBUS | 保留 | 现在由 IP5306 VOUT 供电 (系统 5V 轨) |
| BAT+ / BAT- | 新增 | 电池连接 |
| IP_LED1-3 | 新增 | 电量指示 (可选) |
| CAM_MCLK / CAM_RSTB / CAM_PWDN | 已存在 | 摄像头 (接上) |
| +2V8 / +1V5 | 新增 | 摄像头 LDO1/LDO2 输出 |

> 机械臂走 USB-A 口 (已存在), 无新增网络/元件。

## 七、改动顺序与检查点

1. **改电源** → 跑 review: 确认 VBUS_IN/5V 轨连通, 无悬空。⚠️ 两个坑: (a) IP5306 VIN 必须接 VBUS_IN, 不能接 +5V; (b) **MP2315 EN (pin6) 必须接 +5V (IP5306 VOUT), 不能接 VBUS_IN** — 否则电池模式 Buck 关闭、整板断电。
2. **摄像头** → 跑 review: CAM_* 网接入 FPC, MIPI 差分 (含时钟) 齐全, LDO1/2 输出 +2V8/+1V5 进 FPC。
3. 全部完成 → 全板 review + 同步 BOM/pinmap/schematic-design。
