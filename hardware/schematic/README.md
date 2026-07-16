# 原理图工程 (KiCad)

> 此目录存放 KiCad 原理图（`.kicad_sch`/`.kicad_pro`）。当前为 sheet 规划占位，待选型定稿后在 KiCad 中绘制。

## Sheet 规划（分页）

| Sheet | 文件（拟） | 内容 |
|---|---|---|
| 01 | `01-power.kicad_sch` | USB-C 输入、TVS/eFuse 保护、PD sink、DCDC/LDO 电源树、分区去耦 |
| 02 | `02-p4-core.kicad_sch` | ESP32-P4 模组、复位/BOOT、Flash/PSRAM（裸片时）、晶振、去耦 |
| 03 | `03-c6-radio.kicad_sch` | ESP32-C6 模组、SDIO/SPI 到 P4、天线/射频匹配、EN/BOOT |
| 04 | `04-display.kicad_sch` | MIPI-DSI 连接器、背光升压恒流、复位、触摸（可选 FT5x06） |
| 05 | `05-audio.kicad_sch` | ES7210 + ES8311 + PA + 麦阵 + 喇叭、I2C/I2S、模拟电源/地 |
| 06 | `06-io-sense.kicad_sch` | 4 按键、竖/横槽在位传感器 ×2、可选霍尔/NFC、TF 卡、状态 LED |
| 07 | `07-usb-debug.kicad_sch` | USB-C 数据（OTG）、UART/JTAG 调试口、测试点 |

## 绘制前置
1. 完成 [`../phase0-validation.md`](../phase0-validation.md)，锁定屏/码率/传感器选型。
2. 确认 [`../bom.md`](../bom.md) 中 ⚠️ 项。
3. 依 ESP32-P4/模组 pinout 落实 [`../pinmap.md`](../pinmap.md)。
4. 导入器件库（Espressif 官方符号/封装、ES7210/ES8311 等）。

## 检查
- 完成后跑 **ERC**，与 pinmap/power-tree 对照；网络标号与本文档命名保持一致。
