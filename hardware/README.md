# Hardware (M1) — ESP32-P4 + C6 桌宠板

本目录是 **Milestone 1** 主体：从选型、原理图、PCB 到打样 bring-up。

> **当前状态 (2026-09-25)**: 🔬 V1 板 bring-up 深水区。**已结案 4 项**:
> LDO_VO4（GPIO39~48 那组 IO 的供电，已修进固件）· I2C 总线（async 配置假象，**一个焊点没动**）· SD 卡挂载 · 电源保护。
> **未结案 5 项**: LED 不亮 · MPU-6500 不应答 · 4-bit SD · C6 烧录通道 · `0x28` 未知器件。
>
> 👉 **接手请先读 [`bringup-v1-status.md`](bringup-v1-status.md)** —— 自包含的现状交接（环境/命令/已结案/未结案/坑/文件索引）。
> 下一步: **摄像头 (OV5640 / CN1) 验证**。

## 工作流

```
Phase 0 现成开发板去风险  →  选型定稿  →  原理图(EasyEDA Pro)  →  PCB布局布线  →  打样  →  分区上电 bring-up
   phase0-report.md ✅          bom.md      schematic/ ✅          JLC/*.epro2 ✅   已下单 08-11   （复用 Phase0 固件）
```

## 目录/文档

| 路径 | 内容 |
|---|---|
| [`bringup-v1-status.md`](bringup-v1-status.md) | ⭐ **当前 bring-up 现状交接**（自包含：环境/命令/已结案/未结案/摄像头/已知坑） |
| [`bringup-v1-measure.md`](bringup-v1-measure.md) · [`bringup-v1-ldo.md`](bringup-v1-ldo.md) | V1 逐脚电平扫描 / LDO 调压原始实测记录 |
| [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) | 模组手册 ↔ 网表逐脚对照；§6 是"要问鱼鹰的问题" |
| [`v2-execution-plan.md`](v2-execution-plan.md) | **V2 改板执行方案**（A1~A6） |
| [`v2-netlist-checklist.md`](v2-netlist-checklist.md) | V2 网表逐项改动清单（已落地 10 / 待改 8 / 分支未决） |
| [`phase0-report.md`](phase0-report.md) | **Phase 0 完成**：开发板验证报告（延迟/带宽/内存量化，2026-07 已做） |
| [`bom.md`](bom.md) | 完整物料清单与选型理由 |
| [`bom-lcsc.md`](bom-lcsc.md) | **嘉立创 EDA 导出料单**（48 项 / 81 颗，含 LCSC 编号）；原件 [`BOM_桌面开发助手.xlsx`](BOM_桌面开发助手.xlsx)，下单以 xlsx 为准 |
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
1. ~~**D1 (USBLC6) pin5 = VBUS_IN**~~ ✅ **2026-09-25 网表核实：实际接的是 `+5V`，此疑点不成立**（[`v2-execution-plan.md`](v2-execution-plan.md) A5-3 已勾掉）。
2. **USB_DP/DM（USB-C 调试口 HS）线宽 7.3/10mil**：应为 **9.5mil (90Ω)**，当前不满足（7.3 是 MIPI 的 100Ω 宽度）。HS 480Mbps 阻抗不匹配，下版改宽。
