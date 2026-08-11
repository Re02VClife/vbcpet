# Hardware (M1) — ESP32-P4 + C6 桌宠板

本目录是 **Milestone 1** 主体：从选型、原理图、PCB 到打样 bring-up。

> **当前状态 (2026-08-11)**: ✅ 原理图 + PCB 布局布线完成, **已送打样** (嘉立创, 工程 `ProPrj_桌面开发助手_2026-08-11.epro2`)。待板到货后分区上电 bring-up。
> ⚠️ 待核实: 打样网表里 **D1 pin5=VBUS_IN** 与 **USB_DP/DM 7.3/10mil** 两项疑点（见下方说明）。

## 工作流

```
Phase 0 现成开发板去风险  →  选型定稿  →  原理图(EasyEDA Pro)  →  PCB布局布线  →  打样  →  分区上电 bring-up
   phase0-report.md ✅          bom.md      schematic/ ✅          JLC/*.epro2 ✅   已下单 08-11   （复用 Phase0 固件）
```

## 目录/文档

| 路径 | 内容 |
|---|---|
| [`phase0-report.md`](phase0-report.md) | **Phase 0 完成**：开发板验证报告（延迟/带宽/内存量化，2026-07 已做） |
| [`bom.md`](bom.md) | 完整物料清单与选型理由 |
| [`final-pinmap.md`](final-pinmap.md) | 最终板引脚分配（DSI/I2C/I2S/USB/按键/外设拓展） |
| [`power-tree.md`](power-tree.md) | 电源树与各路电压/电流预算 |
| [`block-diagram.md`](block-diagram.md) | 系统硬件框图 |
| [`modular-design.md`](modular-design.md) | 模块化设计方案（主板元件 vs 外购模块） |
| [`expansion.md`](expansion.md) | **外设拓展方案** (2026-08-04)：电池/视觉回归/机械臂 |
| [`schematic/`](schematic/) | 嘉立创 EDA 原理图设计文档；拓展改动见 [`schematic/expansion-modifications.md`](schematic/expansion-modifications.md) |
| [`pcb-layout-guide.md`](pcb-layout-guide.md) | PCB 布局布线指南（阻抗/线宽/差分约束） |

> **打样工程**: 嘉立创 EDA 专业版工程保存在 `../JLC/ProPrj_桌面开发助手_2026-08-11.epro2`（gitignore，本地）。旧版本 epro2 / 旧网表 .enet / KiCad `pcb/` 占位目录均已清理。

## 关键前提
- P4 无原生 WiFi，联网靠 C6（esp-hosted，SDIO/SPI）。
- Live2D 由 PC 渲染推流，P4 只硬解显示（见 [`../docs/decisions.md`](../docs/decisions.md) ADR-001）。
- Phase 0 已完成，用量化数据锁定了分辨率/码率/内存（见 [`phase0-report.md`](phase0-report.md)）。

## ⚠️ 打样疑点（bring-up 时重点验证）
1. **D1 (USBLC6) pin5 = VBUS_IN**：电池模式下 VBUS_IN=0V，会把 USB-A 外设口 D+/D- 钳到 ~0.7V → **电池模式 USB-A 口可能瘫痪**。设计上应接 `+5V`（见 [`expansion.md`](expansion.md) 七节）。若打样件与网表一致，此点在 bring-up 时验证并计划下版修正。
2. **USB_DP/DM（USB-C 调试口 HS）线宽 7.3/10mil**：应为 **9.5mil (90Ω)**，当前不满足（7.3 是 MIPI 的 100Ω 宽度）。HS 480Mbps 阻抗不匹配，下版改宽。
