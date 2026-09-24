# 嘉立创 EDA 导出料单（LCSC BOM）

> **来源**：[`BOM_桌面开发助手.xlsx`](BOM_桌面开发助手.xlsx) —— 嘉立创 EDA 专业版工程「桌面开发助手」导出的 BOM。
> 本文件是它的**只读镜像**（便于 diff / 检索 / 在 GitHub 上直接看），**下单以 xlsx 为准**。
>
> 规模：**48 个料号行，合计 81 颗**（不含模组内部件与外购模组），供应商均为 **LCSC（立创商城）**，可直接用于嘉立创下单/贴片。

## 与 `bom.md` 的分工

| 文件 | 回答的问题 |
|---|---|
| [`bom.md`](bom.md) | **为什么选它** —— 选型理由、关键参数、替代方案、确认清单 |
| 本文件 | **实际买了什么** —— 型号 + 位号 + 封装 + 数量 + LCSC 编号 |

## 与 V2 改动清单的对应关系

料单里已经落进了 [`v2-execution-plan.md`](v2-execution-plan.md) 的电源保护与 I2C 上拉项：

| 位号 | 料号 | 对应 V2 项 |
|---|---|---|
| **F1** | `SMD0603-050-16`（PPTC 0.5A/16V） | A1-1：F1 由 **0Ω 跳线改成 PPTC 自恢复保险丝** —— V1 烧主控的根因之一 |
| **D3** | `PTVS3V3S1UR-MS` | A1-2：新增 **3.3V 单向 TVS**，防 Buck 下游浪涌灌主控 |
| **D4** | `PTVS5V0S1UR-FS` | A1-3：新增 **5V TVS**（IP5306 VOUT 侧） |
| **F2** | `SMD0805-100-12`（PPTC 1A/12V） | USB 外设口限流 |
| **R9, R18** | 4.7kΩ | A3-1：**I2C SDA/SCL 上拉**到 +3V3 |
| **R14–R17, R11, R8** | 10kΩ ×6 | 各类上拉（位号命名与计划的 R19/R20 不同，以实际网表为准） |

> ⚠️ **修订版本待标注**：从 F1=PPTC、D3/D4=TVS、R9/R18=I2C 上拉这几项判断，这份料单对应的是
> **V2 改动之后的原理图**；若它实际是 V1 打样料单，请更正本节。

## 未计入的外购件

屏体 **YDP430BT009-V1** 与摄像头模组 **OV5640** 是外购模块，不在本料单内 —— 板上对应的是接口件
`FPC30`（30pin 屏座）与 `CN1`（24pin 摄像头座）。主控模组 **ESP32-P4C6-Core**（位号 `M1`）按整模块采购，只有 1 行。

## 完整料单（48 项）

| ID | Name | Designator | Footprint | Quantity | Manufacturer Part | Manufacturer | Supplier | Supplier Part |
|---|---|---|---|---|---|---|---|---|
| 1 | ESP32-P4C6-Module | M1 | ESP32-P4C6-Module | 1 |  |  |  |  |
| 2 | 100nF | C15,C22,C23,C26,C21,C17,C18,C11,C8,C13,C27,C28,C12,C1,C5,C9,C25 | C0603 | 17 | CL10B104KO8NNNC | SAMSUNG(三星) | LCSC | C66501 |
| 3 | TYPE-C16PIN | USB2 | USB-C-SMD_TYPE-C16PIN | 1 | TYPE-C16PIN | SHOU HAN(首韩) | LCSC | C393939 |
| 4 | ZTS6672 | U5,U6 | MIC-SMD_7P-L3.5-W2.7-P0.90-BL_1 | 2 | ZTS6672 |  | LCSC | C9900237692 |
| 5 | KH-FG0.5-H2.0-30PIN | FPC30 | FPC-SMD_KH-FG0.5-H2.0-30PIN | 1 | KH-FG0.5-H2.0-30PIN | kinghelm(金航标) | LCSC | C2797216 |
| 6 | 5.1kΩ | R10,R6 | R0603 | 2 | RCS06035K10FKEA | VISHAY(威世) | LCSC | C4134039 |
| 7 | 10kΩ | R14,R15,R16,R17,R11,R8 | R0603 | 6 | FRC0603J103 TS | FOJAN(富捷) | LCSC | C2930027 |
| 8 | MP2315 | MP1 | SOT-23-8_L2.9-W1.6-P0.65-LS2.8-BL | 1 | MP2315 |  | LCSC | C9900008518 |
| 9 | MT3608 | U2 | SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL | 1 | MT3608 | 西安航天民芯 | LCSC | C84817 |
| 10 | SS14 | D2 | SMA_L4.2-W2.6-LS5.0-RD_1 | 1 | SS14 | MDD(辰达半导体) | LCSC | C2480 |
| 11 | NT3H2111W0FHKH   13.56MHz | NFC1 | XQFN-8_L1.6-W1.6-P0.50-BL_NT3H2111W0FHKH | 1 | NT3H2111W0FHKH | NXP(恩智浦) | LCSC |  |
| 12 | 轴座-凯华轴座+3528反贴灯 1 | SW1 | 凯华轴座下灯位-3528反贴灯 2 | 1 |  |  |  |  |
| 13 | 轴座-凯华轴座+3528反贴灯 2 | SW2 | 凯华轴座下灯位-3528反贴灯 2 | 1 |  |  |  |  |
| 14 | 轴座-凯华轴座+3528反贴灯 3 | SW3 | 凯华轴座下灯位-3528反贴灯 2 | 1 |  |  |  |  |
| 15 | 轴座-凯华轴座+3528反贴灯 4 | SW4 | 凯华轴座下灯位-3528反贴灯 2 | 1 |  |  |  |  |
| 16 | 33Ω | R13 | R0603 | 1 | 1RC0603F3309 | SAE(世盟/世爱) | LCSC | C53224203 |
| 17 | TS342A2P-WZ | RST1,IO33 | SW-SMD_L4.0-W2.9-LS5.0 | 2 | TS342A2P-WZ | SHOU HAN(首韩) | LCSC | C557591 |
| 18 | 20Ω | R4 | R0603 | 1 | 1RC0603J0200 | SAE(世盟/世爱) | LCSC | C54531683 |
| 19 | 22uF | C14,C2 | C0603 | 2 | CL10A226MP8NUNE | SAMSUNG(三星) | LCSC | C86295 |
| 20 | 15Ω | R12 | R0603 | 1 | RC0603FR-0715RL | YAGEO(国巨) | LCSC | C128059 |
| 21 | 40.2kΩ | R5 | R0603 | 1 | 0603WAF4022T5E | UNI-ROYAL(厚声) | LCSC | C12447 |
| 22 | 12.7kΩ | R7 | R0603 | 1 | FRC0603F1272TS | FOJAN(富捷) | LCSC | C2933137 |
| 23 | 10.0 QHHTZB6.3 | USB1 | USB-A-TH_10.0QHHTZB6.3 | 1 | 10.0 QHHTZB6.3 | SHOU HAN(首韩) | LCSC | C668591 |
| 24 | SMD0603-050-16 | F1 | F0603 | 1 | SMD0603-050-16 | R+O(宏嘉诚) | LCSC | C46640964 |
| 25 | USBLC6-2SC6 | D1 | SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL | 1 | USBLC6-2SC6 | ST(意法半导体) | LCSC | C7519 |
| 26 | MPU-6500 | U7 | QFN-24_L3.0-W3.0-P0.40-BL-EP | 1 | MPU-6500 |  | LCSC | C9900017099 |
| 27 | SG90舵机 | U10 | SG90舵机封装 | 1 |  |  |  |  |
| 28 | SG90舵机 | U11 | SG90舵机封装 | 1 |  |  |  |  |
| 29 | SD | TF1 | TF-SMD_TF-15-15 | 1 | SD |  | LCSC | C9900023771 |
| 30 | MAX98357AETE+T | U4 | TQFN-16_L3.0-W3.0-P0.50-BL-EP1.1 | 1 | MAX98357AETE+T |  | LCSC | C20745475 |
| 31 | IP5306 | SOC1 | ESOP-8_L4.9-W3.9-P1.27-LS6.0-BL-EP | 1 | IP5306 | INJOINIC(英集芯) | LCSC | C181692 |
| 32 | 1uH | L1 | IND-SMD_L2.0-W1.6-B | 1 | FTC201610S1R0MBCA | cjiang(长江微电) | LCSC | C5832342 |
| 33 | WAFER-PH2.0-2PWZ | U1,U3 | CONN-TH_2P-P2.00_GP01-2001WR-2P | 2 | WAFER-PH2.0-2PWZ | XUNPU(讯普) | LCSC | C5359610 |
| 34 | KH-FG0.5-H2.0-24PIN | CN1 | FPC-SMD_KH-FG0.5-H2.0-24PIN | 1 | KH-FG0.5-H2.0-24PIN | kinghelm(金航标) | LCSC | C2797213 |
| 35 | AMS1117-1.5 | LOD15 | SOT-223_L6.7-W3.5-P2.30-BR | 1 | AMS1117-1.5 | UMW(友台半导体) | LCSC | C347219 |
| 36 | ME6211A28M3G-N | LOD28 | SOT-23-3_L3.0-W1.7-P0.95-LS2.9-BR | 1 | ME6211A28M3G-N | MICRONE(南京微盟) | LCSC | C236676 |
| 37 | 100uF | C3 | C1206 | 1 | YBK1206X5R107K6R3NT | FH-BK(风华邦科) | LCSC | C55072784 |
| 38 | 470uF | C24 | CAP-TH_BD6.3-P2.50-D1.0-FD_1 | 1 | 470UF10VE110GF | Econd(米朗电子) | LCSC | C46867053 |
| 39 | 4.7uH | L2 | IND-SMD_L7.4-W6.6 | 1 | GSM0630-4R7-M | FAUKU(高芯电子) | LCSC | C51933838 |
| 40 | 10uF | C30,C4,C20,C19,C10,C16 | C0603 | 6 | FCC0603X106K100CT | FOJAN(富捷) | LCSC | C5137565 |
| 41 | 1uF | C29,C31 | C0603 | 2 | CL10A105KO8NNNC | SAMSUNG(三星) | LCSC | C1592 |
| 42 | SMD0805-100-12 | F2 | R0805 | 1 | SMD0805-100-12 | R+O(宏嘉诚) | LCSC | C46640991 |
| 43 | PTVS3V3S1UR-MS | D3 | SOD-123_L2.6-W1.7-LS3.5-RD | 1 | PTVS3V3S1UR-MS | MSKSEMI(美森科) | LCSC | C30602438 |
| 44 | PTVS5V0S1UR-FS | D4 | SOD-123FL_L2.7-W1.8-LS3.8-RD | 1 | PTVS5V0S1UR-FS | FUXINSEMI(富芯森美) | LCSC | C5380694 |
| 45 | PZ254V-11-02P | H1 | HDR-TH_2P-P2.54-V-M | 1 | PZ254V-11-02P | XFCN(兴飞) | LCSC | C492401 |
| 46 | 4.7kΩ | R9,R18 | R0603 | 2 | 0603WAF4701T5E | UNI-ROYAL(厚声) | LCSC | C23162 |
| 47 | MK-12C00-G1.5 | SW5 | SW-SMD_MK-12C00-G1.5 | 1 | MK-12C00-G1.5 | DEALON(德艺隆) | LCSC | C19724092 |
| 48 | 10uH | L3 | L0806 | 1 | AFE201610S100MBT | Magnetsyc(元磁) | LCSC | C41347744 |
