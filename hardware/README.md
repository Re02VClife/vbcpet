# Hardware (M1) — ESP32-P4 + C6 桌宠板

本目录是 **Milestone 1** 主体：从选型、原理图、PCB 到打样 bring-up。

## 工作流

```
Phase 0 现成开发板去风险  →  选型定稿  →  原理图(KiCad)  →  PCB布局布线  →  打样  →  分区上电 bring-up
   phase0-validation.md        bom.md      schematic/        pcb/                    （复用 Phase0 固件）
```

## 目录/文档

| 路径 | 内容 |
|---|---|
| [`phase0-validation.md`](phase0-validation.md) | **先做这个**：现成开发板验证清单（延迟/带宽/内存量化） |
| [`bom.md`](bom.md) | 完整物料清单与选型理由 |
| [`pinmap.md`](pinmap.md) | P4/C6 引脚分配（I2C/I2S/DSI/SDIO/按键/传感器） |
| [`power-tree.md`](power-tree.md) | 电源树与各路电压/电流预算 |
| [`block-diagram.md`](block-diagram.md) | 系统硬件框图 |
| [`schematic/`](schematic/) | KiCad 原理图工程（分 sheet） |
| [`pcb/`](pcb/) | KiCad PCB 工程 + 布线约束 + Gerber |
| `datasheets/` | 器件数据手册（本地存档，勿提交大文件到 git） |

## 关键前提
- P4 无原生 WiFi，联网靠 C6（esp-hosted，SDIO/SPI）。
- Live2D 由 PC 渲染推流，P4 只硬解显示（见 [`../docs/decisions.md`](../docs/decisions.md) ADR-001）。
- **打板前务必完成 Phase 0**，用量化数据锁定分辨率/码率/内存。
