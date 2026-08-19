# vbcpet 下一代板 (V2) 升级路线图

> 2026-08-18 | 基于 Phase 4 最终板 bring-up 的完整操作记录复盘
> 范围：电源保护 / 烧录通道 / 引脚规划 / 外设可靠性 / 屏幕调试 / 装配工艺 / 固件诊断
> 原则：每个 § 都是"踩过的坑 → 根因 → V2 改动"，可直接当迭代清单用。

---

## 0. 本轮 bring-up 事故时间线（催生本路线的直接原因）

1. 板子到手 → 焊好 → 烧录（strap+RST 通道逐步打通）
2. 屏幕上电后曾"插屏拖垮板子不进 ROM"→ 定位为 FPC 接触/座子问题（物理）
3. 调试中 4 颗 SK6812 已改造成状态灯（低亮度蓝/红/黄/绿/灭），验证通过
4. 屏幕背光不亮：GPIO48=1.2V（EN 门槛 1.5V 以下）→ 用 gp 命令隔离出 "pad 活着但网络被拖住"
5. 5V 线误碰板子 GND 打火 → 5V 主干塌到 4.2V → P4 核心 100°C+ → 3.3V 对地短路 → 主控 P4C6-Core 模块 die 烧毁
6. 复盘发现：板上"保险丝"F1 实为 0Ω 跳线，主电源路径没有真保险

**结论先行**：主控烧毁不是"保险丝没挡住瞬态"，而是设计里主电源根本没有保险（F1=0Ω 替保险丝）。V2 第一优先级就是把它补上。

---

## 1. 电源：主供电保护（P0 · 必须）

### 1.1 根因
- bom.md / modular-design.md：F1 | 0Ω | 1206 | USB 跳线(替保险丝) —— 主电源路径用 0Ω 铜跳线代替保险丝
- 唯一 PPTC（F_USB 500mA）在 USB-A 外设口，不在主 USB-C/系统 5V 路径上
- 5V ↔ GND 短路时，电流只受 IP5306 开关管约束，电弧浪涌经 5V→MP2315→3.3V 直灌 P4 die

### 1.2 V2 改动
| 项 | 现在 (V1) | V2 |
|---|---|---|
| USB-C 5V 输入 | 0Ω F1 跳线 | **PPTC 自恢复保险 1A~1.5A** (0805/1206, 遇热断开, 自动恢复) —— 主路径必须真保险 |
| D1 TVS | 有（USB 输入 ESD） | 保留 + 增强：串联方向确认 + 更高钳位能力 |
| 3.3V 主轨 | 无 TVS | **单向 3.3V TVS 到 GND**（防浪涌经 Buck 后灌 P4） |
| IP5306 VOUT(5V) | 无额外保护 | 可加 5V TVS/钳位 |
| 5V/3.3V 大电容 | 部分 | 入口前/后各 ≥10µF 加强浪涌吸收 |

### 1.3 装配/测试要求
- 打样清单把 F1 位号显式标注"保险丝 PPTC（禁 0Ω）"
- 万用表检测项：USB-C 5V↔GND / 3.3V↔GND 冷态电阻 ≤ 保险门槛对照表（附于丝印或 README）

---

## 2. 烧录通道：免 strap 自动下载（P0 · 必须）

### 2.1 根因（踩过的坑）
- **V1 的 USB-C 没引 USJ**：板子 USB-C 走的是 USB2 HS UTMI（pin48/49 = USB2 OTG），而 P4 原生 USJ 在 GPIO24/25（FSLS PHY0）——没布到任何 USB 座，所以只能靠 GPIO35/36 硬 strap + RST1 进下载，绕了一整轮。
- **GPIO24/25 还被舵机占了**（servo_init LEDC），即使飞线 USB 信号到 GPIO24/25 能枚举（PID_0012→1001），烧录时也会被舵机抢焊盘而不稳。
- esptool 两段式（read_mac 后 write_flash）会被 RAM-stub 互相踩 → 必须单进程 --no-stub。
- 后来用固件 dl 命令（置 LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT + esp_restart()）实现免 strap 重烧——已在板上验证 DL-FLASH-OK hashes=3（仍是经 CH340/UART0）。

### 2.2 V2 改动 — ★主线: USB-Serial/JTAG 即插即用★

V2 的目标是**一根 USB-C 线即插即用**(像开发者板上电-烧录-串口-JTAG 全自动), **不需要** CH340、不需要烧录排针、不需要 DTR/RTS 自动复位电路。

| 项 | 说明 |
|---|---|
| ★★ 调试口 = 板载 USB-C(引 USJ) | 把 GPIO24/25 (USJ FSLS PHY0) + 5V + GND 布到**板上一个标准 USB-C**（DP/DM 经 ESD 直连, VBUS 连 5V）→ 一条线 = 供电+烧录+串口日志+JTAG。V1 已验证此路可枚举(PID_0012→1001), 只是没引到 USB-C |
| ★ esptool 直连 | 电脑 USB 插板载 USB-C → esptool 直接识别这个 USB 口烧录, 无需 --before 复位/strap；USJ 原生 ROM 下载+自动复位 |
| ★ OpenOCD/JTAG | 同一线可做 JTAG 调试(firmware/esp32p4-usj-flash.cfg + usj_flash.tcl 已备好), V1 因 GPIO24/25 被舵机占用而没稳定, V2 排他后即用 |
| 固件 dl 命令兜底 | 保留 dl(置 FORCE_DOWNLOAD_BOOT+restart, V1 已验证 hashes=3) 作**远程/无壳重烧**兜底, 出厂必带 |
| (可选备用) CH340 排针 | 仅当想用独立 UART0 外接串口时加 2×2 (RX/TX/GND/5V), **非必需**; V1 的 UART0+CH340 方案降级为备用 |
| esptool 经验固化 | 脚本统一 --no-stub 单进程(V1 踩坑), 写入 firmware/ 文档 |

---

## 3. 引脚规划：USB 外设口 vs 模组功能脚（P0 · 必须）

### 3.1 根因（本轮最烧脑的认知更替）
- 曾结论"USB-C D+ = 主控坏"，后被推翻：USB-Serial/JTAG 的 FSLS PHY 在 GPIO24/25（PHY0），不在模组 pin48/49（HS UTMI/板子 USB-C）
- GPIO24/25 同时被 servo_init 配成 LEDC PWM（舵机 YAW/PITCH）→ 与 USB-Serial/JTAG 抢焊盘，烧录时 USB 闪烁
- 模组 pin53/54 = GPIO26/27 = USB1 FS OTG（USB-A 外设口）与 USB-Serial/JTAG 是两个不同 PHY

### 3.2 V2 改动
| 项 | 说明 |
|---|---|
| ★★ GPIO24/25 = 板载调试 USB-C | **硬路由**到板载 USB-C 的 DP/DM（FSLS PHY0, USJ），作主调试口；一条线即插即用 |
| ★ USB-Serial/JTAG 引脚排他 | GPIO24/25 之后**禁用**给舵机等任何外设（这是 V1 USJ 不稳定的根因）；硬件脚位规划里划死 |
| 舵机改脚 | YAW/PITCH 移到 28/29(已预留 SERVO3/4) 或不冲突脚 |
| USB-C 双角色规划 | 调试口(USJ, GPIO24/25)与 外设 USB-A(USB1 OTG, GPIO26/27 pin53/54) 互不干扰, 一人一个 PHY; 也可把 USB1 OTG 引到另一个 USB-C |
| 电源/数据分离 | 外设 USB-A 的 VBUS 经 PPTC(文档已规定 500mA), 调试口 VBUS 直连供电 |

---

## 4. 外设可靠性：I2C / 上拉 / 器件地址（P1 · 高）

### 4.1 根因
- 400kHz I2C 全超时 → 降到 100kHz 后仍需"直读法"（i2c_master_probe 在 async 驱动下不可靠）才能看清总线
- 疑似缺外部上拉/上拉过弱；MPU-6500(U7) 疑似焊失败（WHO_AM_I 读回 0x00）；NFC 未焊

### 4.2 V2 改动
| 项 | 说明 |
|---|---|
| I2C 外部上拉确认 | SDA/SCL 各 4.7kΩ 上拉至 3.3V，焊在 PCB（双端可选直连）；bom 强制项 |
| 上电时序 | I2C 上拉在 3.3V 建立后再使能，避免漏电流 |
| MPU 地址/姿态 | U7 必须补焊；焊后固件用 rd 68 75 / rd 69 75 判别 AD0（0x68/0x69）并存档 |
| 触摸地址 | TOUCH_I2C_ADDR 0x24 待实板探测确认，V2 屏 FPC 上留 touch 使能引脚方便测量 |
| NFC | NT3H2111 补焊 + FD 上拉（文档已列），V2 列入必测项 |

---

## 5. 屏幕 / FPC / MIPI-DSI（P1 · 高）

### 5.1 根因
- 屏 FPC 插上曾拖垮板子不进 ROM（物理接触/座子问题）
- lcd 命令强制初始化 DSI 时卡在 DSI HOST busy-wait（lane 未到屏）；固件已加 bl 背光隔离命令
- DSI lane 顺序（模组 vs FPC）是否交叉未证实

### 5.2 V2 改动
| 项 | 说明 |
|---|---|
| FPC 座子选型 | 翻盖/掀盖(flip-lock) 30pin 座，丝印 1 脚三角形标记 + 箭头；焊盘加大便于目检 |
| 屏供电/复位时序 | 屏 VDD 3.3V 直连（已确认）；LCD_RST(GPIO20) 时序在初始化序列里显式控制；背光 LEDK 单独可测点 |
| DSI 测试点 | DSI 3 对差分各拉 2 个 probe 焊盘，便于示波器验证 lane 顺序/极性（V1 缺了这步，导致带屏怀疑卡死无法确认） |
| 背光链路可修 | GPIO48→MT3608 EN；MT3608 SOT-23-6 方向丝印要清晰（本轮怀疑焊反），bom 标注 pin1 |
| 固件侧 | 保留 lcd（强制 DSI，带超时/防卡死）与 bl（背光隔离）两个命令，V2 出厂必含 |

---

## 6. 装配与工艺（P2 · 中）

### 6.1 根因
- P4C6-Core 88pin castellation 手工焊接：GPIO48 网络"pad 活着但外部 1.2V"疑焊点虚/冷；整体可靠性依赖焊工
- LED/座子等手工件定位难

### 6.2 V2 改动
| 项 | 说明 |
|---|---|
| 主控焊接 | 推荐钢网+锡膏+回流焊；手工补焊后必做"引脚连续性扫描表"（每个 castellation 脚对测试点蜂鸣） |
| 测试点面板 | 主板预留 1×10 测试点排针：3V3/5V/GND/UART TX/RX/GPIO24/25/GPIO48/I2C_SCL/SDA —— 免飞线调试 |
| 定位/防呆 | USB-C、FPC、按键、SD 全部做丝印方向标记，PCB 上标注位号+方向箭头 |

---

## 7. 固件诊断基础设施（P1 · 高，已部分完成）

### 7.1 本轮已落地（保留到 V2）
- UART0 控制台：help/list/test all/scan/reboot/dl/rd/gp/led/lcd/bl/brightness
- 状态灯：SK6812×4 低亮度（蓝启动/绿 ok/黄 warn/红 err/灭）
- 直读法 I2C 扫描、rd 裸读寄存器、gp 强制 GPIO 摆幅测量
- dl 软件强制下载（免 strap 重烧闭环）

### 7.2 V2 增量
| 项 | 说明 |
|---|---|
| 控制台僵化修复 | 长串口会话后 UART0 静默（需 RST 恢复）——加空闲看门狗/线程保活，列入固件 TODO |
| 启动自检固化 | 开机自动跑最小自检，状态灯汇报；错误码 LED 闪烁次数编码 |
| 回读差分对 | 固件加 DSI lane 回读（lane 数/极性寄存器 dump），替代示波器做初判 |

---

## 8. 优先级路线图

| 优先级 | 模块 | 内容 | 花费估计 |
|---|---|---|---|
| P0 | §1 电源保护 | 真保险 PPTC + 3.3V/5V TVS + 入口电容 | 低 |
| P0 | §2 自动烧录 | ★ 板载 USB-C 引 USJ(即插即用) + dl 兜底; CH340/排针降为可选 | 低 |
| P0 | §3 引脚排他 | GPIO24/25 归 USB-Serial/JTAG，舵机改脚 | 中（改板） |
| P1 | §4 I2C 上拉 | 4.7k 上拉 + MPU 补焊验证 | 低 |
| P1 | §5 屏幕 | flip-lock 座 + DSI 测试点 + lane 验证流程 | 中 |
| P1 | §7 固件 | 控制台看门狗 + 开机自检汇报 | 低（纯固件） |
| P2 | §6 工艺 | 钢网回流 + 测试点排针 + 丝印防呆 | 中 |

> 建议顺序：先 P0（电源+烧录+引脚）重出 V2 板 → P1（I2C/屏幕/固件）在 V2 板上继续 bring-up → P2 量产工艺。

---

## 附录 A：V2 新增/修改位号速查

| 位号 | V1 | V2 | 说明 |
|---|---|---|---|
| F1 | 0Ω 1206 | **PPTC 1A~1.5A 0805/1206** | 主电源保险 |
| D1 | TVS | 保留 + 加强 | USB 输入保护 |
| — (新增) | — | TVS_3V3 | 3.3V 主轨 TVS 到 GND |
| — (新增) | — | R8/R9 4.7k | I2C SDA/SCL 上拉 |
| — (新增) | — | J_DEBUG (2×2) | ★可选*: 独立 UART0 排针 RX/TX/GND/5V(用 USJ 则不需要) |
| — (新增) | — | TP_10 (1×10) | 测试点排针 |
| 舵机脚 | GPIO24/25 | 改 28/29 或复用他脚 | 让位 USB-Serial/JTAG |

## 附录 B：关联文档
- 电源拓扑: power-tree.md / schematic/schematic-design.md / modular-design.md
- 引脚: final-pinmap.md / board_config.h
- 烧录经验: 本项目 _dlflash5.ps1 等根目录脚本 + firmware/README.md
- 本轮事故教训: WORKLOG 2026-08-18 条目
