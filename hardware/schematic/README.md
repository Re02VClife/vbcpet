# 原理图工程 — 嘉立创 EDA 专业版

> **状态 (2026-08-11)**: ✅ 原理图已绘制完成（5 页 + 外设拓展实装），网表复核通过，**已随打样定稿**。当前工程：`../../JLC/ProPrj_桌面开发助手_2026-08-11.epro2`（本地 gitignore）。
> 历史过程文档：[schematic-design.md](schematic-design.md)（基础 5 页设计，位号为重排前）+ [expansion-modifications.md](expansion-modifications.md)（外设拓展改动，已实装）+ [connection-fix.md](connection-fix.md)（断线修复）。
> ⚠️ **位号已重排**：早期文档里的 U7(MP2315)/U9(MT3608)/U12(功放)/U16(NFC) 等位号在当前网表中已变更，**以 EDA 工程网表为准**。

## 前置准备

1. 安装 [嘉立创 EDA 专业版](https://pro.lceda.cn/) (桌面版)
2. 新建工程 `vbcpet-board`（现为 `桌面开发助手`）
3. 创建 5 页原理图 Sheet

## Sheet 规划

| Sheet | 名称 | 内容 |
|-------|------|------|
| 01 | 电源 | USB-C + CH224K(可选) + MP2315 Buck + MT3608 背光 |
| 02 | 主控 | P4C6-Core 模组 + Strapping + 总线引出 |
| 03 | 显示 | FPC 30pin + 触摸 3.3V 直连 + 背光 LED |
| 04 | 音频 | MAX98357A + ZTS6672 x2 + 喇叭 |
| 05 | I/O | 4 凯华轴座 + SK6812 3215 ×4 + NT3H2111 NFC |

> **外设拓展 (2026-08-04)**: Sheet 01 增 IP5306+电池, Sheet 05 增摄像头 FPC/雷达/机械臂 — 改动步骤见 [expansion-modifications.md](expansion-modifications.md)。

## 绘制步骤

### 1. 导入 BOM

> ⚠️ 旧的 `bom-lcsc.csv`（外设拓展前，含错误位号/错误分压值）已删除。当前 BOM 以 **嘉立创 EDA 工程** 为准：`设计 → 导出 BOM` 自动生成含 LCSC 编号的清单。

### 2. 按 Sheet 绘制

参考 [`schematic-design.md`](schematic-design.md) 中的逐页连接表：
- 每页列出所有元件、引脚连接、网络名
- 电源网络用全局标签 (`+3V3`, `VBUS`, `GND`, `LED_A`)
- 总线信号用 Sheet 间端口 (`I2C_SDA`, `I2S_BCLK` 等)

### 3. 自建元件

以下元件在嘉立创 EDA 库中不存在或需确认，请手动创建符号+封装：

| 元件 | 原因 |
|------|------|
| **ESP32-P4C6-Core** | 鱼鹰光电定制模组, 88pin 邮票孔 |
| **YDP430BT009-V1 FPC** | 30pin 0.5mm FPC 连接器 (或用库中兼容型号) |
| **凯华热插拔轴座** | Kailh CPG151101S11，6pin |
| **SK6812 mini-E 3215** | 反贴 RGB LED，3.2×1.5mm |
| **IPEX1 天线座** | 可找库中现有封装 |

### 4. 完成后

- 运行 **DRC** (设计规则检查)
- 对照 [final-pinmap.md](../final-pinmap.md) 验证引脚分配
- 导出 BOM 与 [`bom.md`](../bom.md) 交叉核对
- 用 [`tools/schematic_review.py`](../../tools/schematic_review.py) 跑网表自动审查

## 关键布线约束 (Layout 时注意)

| 信号 | 约束 |
|------|------|
| DSI 差分对 | 100Ω 差分阻抗, 对内等长 ±0.25mm, 包地 |
| USB 差分对 | 90Ω 差分阻抗, 等长 ±1mm |
| C6 RF 天线 | 50Ω 单端, 短直, 0 过孔 |
| I2S 时钟 | BCLK/WS 等长 ±20mm (3MHz 低速) |
| NFC 天线 | PCB 线圈 3~4 匝 25×25mm, 下方禁铺地 |
| 电源 | Buck/Boost SW 节点最短, FB 远离 SW, 输入电容紧贴 IC |
| SK6812 LED | 5V VBUS 供电, 每灯 100nF 旁路, 数据线 33Ω 匹配 |

> 详见 [`../pcb-layout-guide.md`](../pcb-layout-guide.md)

## 参考文件

| 文件 | 说明 |
|------|------|
| [schematic-design.md](schematic-design.md) | 基础 5 页设计文档（位号重排前，已过时） |
| [expansion-modifications.md](expansion-modifications.md) | 外设拓展改动方案（已实装） |
| [connection-fix.md](connection-fix.md) | 断线修复记录 |
| [../bom.md](../bom.md) | BOM 总表 |
| [../final-pinmap.md](../final-pinmap.md) | 最终板引脚分配 |
| [../power-tree.md](../power-tree.md) | 电源树 |
| [../pcb-layout-guide.md](../pcb-layout-guide.md) | PCB 布局布线指南 |
