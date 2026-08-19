# vbcpet V2 板落地执行方案 (P0+P1) - 2026-08-18

> 依据: upgrade-roadmap-v2.md 的 P0+P1 范围, 基于 EasyEDA 工程 JLC/ProPrj_桌面开发助手_2026-08-11.epro2 迭代.
> 目标: 出一版"烧不坏 + 一根 USB 线即插即用 + 调试友好"的 V2 板, 主控不因电源事故/引脚冲突再挂.

---

## A. 原理图改动清单 (EasyEDA 工程内逐项操作)

### A1. 电源保护 (P0) - Sheet 01 电源

| # | 位号 | 现在 | 改成 | 操作/验证 |
|---|---|---|---|---|
| 1 | F1 | 0Ω 1206 跳线 | PPTC 自恢复 1A~1.5A (0805/1206) | 换封装+物料; BOM 标注"禁0Ω"; 打样后 5V短GND 应自动保护 |
| 2 | (新) TVS_3V3 | - | 单向 3.3V TVS 3.3V→GND | 新增元件; 防 Buck 下游浪涌灌主控 |
| 3 | (可选) TVS_5V | - | 5V 轨 TVS 到 GND | 若空间允许, 保护 IP5306 VOUT 侧 |
| 4 | C 输入/输出 | - | 5V 入口前后各 ≥10µF; 3.3V 输出 ≥10µF+100nF | 增强浪涌吸收与退耦 |

### A2. 烧录/调试口 = USJ 即插即用 (P0) - Sheet 02 主控

| # | 项 | 说明 |
|---|---|---|
| 1 | ★ GPIO24/25 引到板载 USB-C | 把 USJ FSLS PHY0 引到板上标准 USB-C (DP/DM 依模组映射核对) + VBUS/GND; 一条线即插即用 |
| 2 | ESD 保护 | USB DP/DM 用 USBLC6 或同类 TVS; 差分 90Ω 等长 ±1mm |
| 3 | GPIO24/25 排他 | 原理图删除 SERVO1_YAW/GPIO24、SERVO2_PITCH/GPIO25 网络; 改接 GPIO28/29 |
| 4 | 舵机改脚 | YAW→GPIO28, PITCH→GPIO29 (预留位已存在); board_config.h 同步改 |
| 5 | (可选) UART0 排针 2×2 | RX/TX/GND/5V 备用; 用 USJ 则不需要 |

### A3. I2C 上拉 (P1) - Sheet 02/03

| # | 项 | 说明 |
|---|---|---|
| 1 | R8/R9 4.7k 上拉 | SDA/SCL 各一颗 4.7kΩ 到 +3V3, 焊在 PCB 上 |
| 2 | I2C 速度 | 固件 100kHz 保留 (弱上拉兼容), 上拉后试回 400kHz |

### A4. 屏幕/FPC/DSI (P1) - Sheet 03 显示

| # | 项 | 说明 |
|---|---|---|
| 1 | FPC 座 | 换翻盖/掀盖(flip-lock) 30pin 0.5mm 座, 丝印 1 脚三角+箭头 |
| 2 | DSI 测试点 | DSI 三对差分各拉 2 probe 焊盘 (D0P/N, D1P/N, CLKP/N) |
| 3 | MT3608 丝印 | SOT-23-6 印 pin1 标记 (圆点/斜角), BOM 注明引脚定义 |
| 4 | LCD_RST 时序 | 保持 GPIO20; 背光 LEDK 拉到可测点 |

### A5. 已知打样疑点一并修 (V1 遗留)

> 来自 PROGRESS 下一版修正:

| # | 项 | 说明 |
|---|---|---|
| 1 | L1 (IP5306) | 201610→CD32 电感 |
| 2 | LED_A/+2V8 线宽 | 从 12mil 加宽 (承载 40mA+浪涌) |
| 3 | D1 pin5 | →+5V (勿 VBUS_IN) - 电池模式 USB-A 才不瘫 |
| 4 | USB_DP/DM | 9.5mil (±10% 90Ω diff) |

---

## B. 并行固件任务 (现有 P4 开发板平台做, 不等 V2 板)

| # | 任务 | 说明 | 归属 |
|---|---|---|---|
| B1 | 控制台"僵化"修复 | UART0 长会话后停响应 - 加空闲看门狗/队列保活; 开发板复现验证 | 固件 |
| B2 | 背光链路驱动固化 | bl/brightness 完善; MT3608 EN 时序 | 固件 |
| B3 | DSI ST7102 lane 预研 | 读 esp_lcd_st7102 init_cmds, 备 lane swap/极性寄存器方案, V2 到手即试 | 固件 |
| B4 | 开机自检+状态灯巩固 | boot 自动 selftest, LED 闪烁次数编码错误码 | 固件 |
| B5 | dl 通道文档化 | 烧录流程固化到 firmware/README (USJ 为主, dl 兜底) | 固件+文档 |

> 开发板平台: WT9932P4-TINY (SPI 后端). 注: DSI/屏幕/外设在开发板无对应硬件, 固件可编译+逻辑验证, 硬件联调等 V2 板.

---

## C. 里程碑与顺序

| 里程碑 | 内容 | 前置 | 预估 |
|---|---|---|---|
| M1 原理图改版 | A1-A5 全部落到 epro2, 网表复核 (tools/schematic_review.py) | EasyEDA 工程在手 | 1-2 天 |
| M2 PCB 改版 | 更新布线: 新 USB-C/DSI 测试点/舵机移位/叠层保持 JLC04161H-7628 | M1 | 2-3 天 |
| M3 DRC+导出 | DRC 通过, 出 gerber/BOM(含 LCSC 号) | M2 | 0.5 天 |
| M4 打样下单 | JLC 4 层板 下单 (沿用免费叠层) | M3 | 下单后 ~1-2 周到货 |
| M5 V2 bring-up | 复用现有烧录(USJ/dl)+诊断命令做外设逐项验证 | M4 到货 | 2-4 周 |
| M6 满功能回归 | 480×800 推流 + 触摸 + NFC + 屏幕 DSI 点亮 | M5 | 2 周 |

> 并行: M4 等交期窗口把 B1-B5 固件任务做完, V2 到手即联调.

---

## D. 风险与对策

| 风险 | 对策 |
|---|---|
| USJ 引脚映射拿错 | 核对模组 datasheet GPIO24/25 与 USB_DP/DM 对应, 先飞线验证再定 PCB |
| PPTC 误选 0Ω/型号不对 | BOM 强制"PPTC 禁0Ω", 料单审批 |
| DSI lane 顺序仍未知 | 提前 B3 预研, V2 备 lane swap 寄存器方案 |
| 打样交期长 | 固件 B1-B5 打样期间完成, 空档不浪费 |
| V1 经验不回传 | V1 bring-up 的测量/命令/坑全部沉淀 WORKLOG/固件, V2 复用 |

---
## 附: 已完成前置 (本次会话)
- [x] 烧录管线 (strap+RST + dl 免strap) - 已验证
- [x] SK6812 状态灯 (低亮度 5 态) - 实板通过
- [x] 诊断命令 lcd/bl/gp/rd/scan - 就绪
- [x] V2 路线图 upgrade-roadmap-v2.md
- [x] 本项目临时测试文件清理 (148 项)
