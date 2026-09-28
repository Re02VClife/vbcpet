# vbcpet V2 板落地执行方案 (P0+P1) - 2026-08-18

> 依据: upgrade-roadmap-v2.md 的 P0+P1 范围, 基于 EasyEDA 工程 JLC/ProPrj_桌面开发助手_2026-08-11.epro2 迭代.
> 目标: 出一版"烧不坏 + 一根 USB 线即插即用 + 调试友好"的 V2 板, 主控不因电源事故/引脚冲突再挂.

---

## A. 原理图改动清单 (EasyEDA 工程内逐项操作)

### A1. 电源保护 (P0) - Sheet 01 电源

| # | 项 | V1 | 09-15 板现状 (已从网表核对) | 还要做什么 |
|---|---|---|---|---|
| 1 | 主 3.3V 路径保险 | 无 (F1 = 0Ω 跳线替保险) | ✅ `F2 = SMD0805-100-12` (0805, **1A PPTC**), 位置 `+5V → MP2315.IN` | 已满足。BOM 强制标注"PPTC 禁 0Ω" |
| 2 | USB-A 口保险 | F_USB 500mA PPTC | ✅ `F1 = SMD0603-050-16` (0603, 0.5A PPTC), 位置 `+5V → USB1.VCC` | 已换。⚠️ **F1 这个位号在两版含义不同**: V1 的 F1 在主路径, 09-15 的 F1 是 USB-A |
| 3 | 3V3 TVS | 无 | ✅ `D3 = PTVS3V3S1UR-MS` (3V3↔GND) | 已完成 |
| 4 | 5V TVS | 无 | ✅ `D4 = PTVS5V0S1UR-FS` (+5V↔GND) | 已完成 |
| 5 | 输入/输出电容 | 部分 | ✅ `C30 10µF @VBUS_IN`; `C14 22µF`+`C19/C20 10µF` @3V3; `C16/C24 10µF+470µF` @+5V | 已满足 |
| 6 | ⚠️ 剩余缺口 | — | `VBUS_IN → IP5306.VIN` 这一段**只有 C30, 没有保险** | 可选: 加一颗 PPTC, 或确认 IP5306 自带输入过流保护后接受 |

### A2. 烧录/调试口 = USJ 即插即用 (P0) - Sheet 02 主控

| # | 项 | 说明 |
|---|---|---|
| 1 | ★ GPIO24/25 引到板载 USB-C | ✅ **09-15 网表已完成**: `USB2.A6/B6(DP) → USB_A_DP0 → M1.51`、`USB2.A7/B7(DN) → USB_A_DM0 → M1.50`、`CC1/CC2 → R6/R10 5.1k → GND`、`VBUS → VBUS_IN → IP5306.VIN`。
|   |   | 映射已确认 (不再是待核对项): IDF `io_mux_reg.h` 的 `USB_INT_PHY0_DM_GPIO_NUM 24` / `USB_INT_PHY0_DP_GPIO_NUM 25` ⇒ **GPIO24 = D-, GPIO25 = D+**; V1 飞线实测枚举过 (PID_0012→1001)。
|   |   | ⚠️ 网络名 `USB_A_DM0/DP0` 与真 USB-A 的 `USB_A_DM/DP`(GPIO26/27) 只差一个 0, 建议改名 `USJ_DM/DP` |
| 2 | ESD 保护 | USB DP/DM 用 USBLC6 或同类 TVS; 差分 90Ω 等长 ±1mm |
| 3 | GPIO24/25 排他 | 原理图删除 SERVO1_YAW/GPIO24、SERVO2_PITCH/GPIO25 网络; 改接 GPIO28/29 |
| 4 | 舵机改脚 | YAW→GPIO28, PITCH→GPIO29 (预留位已存在); board_config.h 同步改 |
| 5 | (可选) UART0 排针 2×2 | RX/TX/GND/5V 备用; 用 USJ 则不需要 |
| 6 | ★ **C6 串口编程器通道** | 🔴 **09-15 网表未连** (`M1.4/5/9` 全 NC; P4 侧 `M1.58/59/61 = GPIO31/32/34` 也全 NC, 空闲可用)。
|   |   | 接法: `C6_U0RXD(pin4) ← GPIO31(pin58)`、`C6_U0TXD(pin5) → GPIO32(pin59)`、`C6_IO9(pin9) ← GPIO34(pin61)` + IO9 10k 上拉 3V3。P4 侧电平 2026-09-24 实测 3.24V ✅。
|   |   | ⚠️ **但功能可行性从未验证过**: `pin5` 实测 0.12V ⇒ C6 可能没在跑 (见 [p4c6-manual-crosscheck.md](p4c6-manual-crosscheck.md) §3.5b, 三种可能一种都没排除); 且模组**未引出 C6 的 EN** ⇒ 进下载模式只能整板断电。**先问原厂"C6 出厂有没有预烧 esp-hosted slave、能不能 OTA"再决定这 3 根线要不要走** |

### A3. I2C 上拉 (P1) - Sheet 02/03

| # | 项 | 说明 |
|---|---|---|
| 1 | ~~R8/R9~~ → **R9/R18** 4.7k 上拉 | ✅ **09-15 网表已有, 本项无改动**: `R9 4.7k → I2C_SCL`、`R18 4.7k → I2C_SDA`, 均到 3V3。⚠️ **勿动 `R8`** —— 那是 `SD_CD` 的 10k 上拉, 与 I2C 无关 (旧稿位号写错) |
| 2 | I2C 速度 | 上拉已具备 ⇒ `board_config.h` 里那句"疑似缺外部上拉"的注释应删掉, 别再按这个思路排查; 固件可试回 400kHz |

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
| 5 | D5 (新) | USJ(GPIO24/25) D± 直连 USB-C, 无 ESD → 加 USBLC6-2SC6 (SOT-23-6) |
| 6 | R19 (新) **上拉** | `GPIO35 → 10k → 3V3`。手册 pin62 明写"模组内部无上拉", 而 `SW5` 只把 GPIO35 拉向 GND ⇒ **不按时靠它给 1**。V1 就是这颗上拉开路导致整轮烧录失败 |
| 6b | R20 (新) **上拉** (或**不装**) | `GPIO36 → 10k → **3V3**`。⚠️ **绝不能下拉到 GND** —— 手册 §3.3.2: Joint Download Boot = `GPIO35=0 + **GPIO36=1**`, 把 GPIO36 钉在 0 会**让下载模式永远进不去**。2026-09-25 实板验证: **只用 GPIO35 接地就能进下载** ⇒ GPIO36 浮空时已被读作 1。装上拉只是不依赖浮空的运气; 装了就顺带把冗余的 `SW6`(GPIO36→3V3) 删掉 |
| 7 | D6 (新) | VBUS_IN→+5V 无单向 OR → 加 SS34, 否则无电池时 IP5306 VOUT≈1.9V, 插 USB 也点不亮整板 |
| 8 | **删** M1.76(GPIO48)→3V3 | 09-15 板把背光 PWM 网整条挂到 3V3 = 反灌; 按 A6 改法恢复 |

### A6. ★ IO 供电矫正: GPIO39~48 组 + pin66 (2026-09-24 V1 实板实测 / 2026-09-25 手册定性)

> 依据: 逐脚电平扫描 (`tools/gp_sweep.py`) + IDF 源码核对。详见 [p4c6-manual-crosscheck.md](p4c6-manual-crosscheck.md) §3.1b / §7。

**实测事实**

- 模组 P4 的 **GPIO39~48** 这一组 pad, IO 供电不是 3.3V, 而是 **~1.23V**。
  推挽输出高电平实测: GPIO45 = 1.23V, GPIO46 = 1.23V, GPIO47 = 1.23V, GPIO48 = 1.33V。
- 这一组覆盖: SD 卡 CLK/CMD/D0-3 (39-44) / SD_CD (45) / **PA_EN (46)** / **MPU_INT (47)** / **BL_PWM (48)**。
  ⇒ 一次性解释: SD 卡 `sdmmc_card_init failed`、功放使能拉不起、MPU 中断收不到、背光 PWM 驱不动 MT3608 EN。
- **软件改不动**: `ldo 4 800 / 2000 / 3300` 全部返回 `OK` 但 pin66 恒为 1.24V。
  IDF `esp_ldo_regulator.c` 路径已逐行核对无 bug (unit 映射 / dref-mul / owner=SW 都对)
  ⇒ 旧结论"该轨不由 LDO_VO4 驱动"**已于 2026-09-25 推翻** —— 见下方"手册定性"。
    它**就是** LDO_VO4, 只是软件路径没作用到这个节点 (原因待查, 见 §A6-补 三步实验)。
- **pin66 (手册标 ESP_LDO_VO4) 实测就是这组 IO 的电源节点**:

  | 外部从 3V3 串入 | pin66 / GPIO48 实测 | 推算 |
  |---|---|---|
  | 4.7 kΩ | 2.35 V | I = (3.3−2.35)/4.7k = **202 µA** |
  | 4×4.7 kΩ 并联 = 1.175 kΩ | 2.80 V | I = (3.3−2.80)/1.175k = **425 µA** |

  pin66 与 GPIO48 **始终同步变化** ⇒ pin66 是这组 IO 的供电节点, 而非孤立的 LDO 输出。
  但注意电流随电压上升 (202µA@2.35V → 425µA@2.80V, 等效负载 11.6kΩ → 6.6kΩ),
  **负载非线性**; 1.175kΩ 只到 2.80V, 要顶到 3.0V+ 需要 **470Ω 或更小**。

**手册定性 (2026-09-25 补)**

> 手册 pin66 原文: `Output power supply (output voltage range 0.5–2.7V or 3.3V, maximum output current 0.2A)`

| 措辞 | 含义 | IDF 对照 |
|---|---|---|
| **Output** power supply | 它是**电源输出脚**, 不是 GPIO, 不是输入 ⇒ 设计意图是"外部从这里取电" | `sd_pwr_ctrl_by_on_chip_ldo.h`: *"set to `4` is the `LDO_VO4` is connected to power the SDMMC IO"* |
| 0.5–2.7 V | 连续可调档 | `LDO_LL_RECOMMEND_MIN/MAX_VOLTAGE_MV = 500 / 2700` |
| or 3.3 V | **旁路档**, 不是"调到 3.3V" | `LDO_LL_RAIL_VOLTAGE_MV 3300`; `use_rail_voltage = (voltage_mv == 3300)`, 注释 *"bypass the LDO"* |
| max 0.2 A | 上限 200mA ⇒ 只够这组 IO pad, **不得**拿去供屏/摄像头/舵机 | 实测这组负载仅 202~425µA |

⇒ 三条独立证据 (手册 pin66 / IDF sdmmc 注释 / 实测"pin66 与 GPIO48 同步") 指向同一结论:
**GPIO39~48 这组 pad 的 IO 供电轨本来就由 LDO_VO4 提供 —— 这是标准设计, 不是本板缺陷。**
V1 上 SD/PA_EN/MPU_INT/BL_PWM 全瘫的根因是**固件从未配置该 LDO**, 复位后停在默认 ~1.2V。

⚠️ **因此"外部串阻灌 3.3V"是反灌一个输出脚**, 不应作为主方案。

---

### ✅ 2026-09-25 实测定案: LDO_VO4 完全可控, 通道号就是 4

拆掉 pin66 上的外部连线后, 在 V1 上实发三条命令 (串口日志 + 万用表):

| 命令 | pin66 实测 | pin76 (GPIO48) 实测 | 回显 |
|---|---|---|---|
| `ldo 4 1800` | **1.8 V** | **1.8 V** | `-> 1800 mV: OK` |
| `ldo 4 2700` | **2.7 V** | **2.7 V** | `-> 2700 mV: OK` |
| `ldo 4 3300` | **3.3 V** | **3.3 V** | `-> 3300 mV: OK` |

⇒ pin66 与 GPIO48 **同步跟随**, 三档全中 ⇒ **通道 4 正确, 这条轨完全由软件支配**。

> ❌ **作废**: 本文件 2026-09-24 记的"软件改不动 / 该轨不由 LDO_VO4 驱动"。那是**假象** ——
> `tools/serial_console.py` 当天的首字符丢失 bug 会把 `ldo 4 3300` 吃成 `?ldo` ⇒ 未知命令,
> 命令根本没到板子。该 bug 同一天已修 (连接后先发换行热身)。

**V2 操作 (已定稿)**

| # | 项 | 说明 |
|---|---|---|
| 1 | **pin66 = NC** | **不接任何电源**。该轨由 P4 内部 LDO_VO4 提供; 外部一接就是反灌一个输出脚 |
| 2 | (可选) pin66 留一个**测试点** | 只为复测方便。**不要**留到 3V3 的电阻位 —— 已确认不需要, 留着反而是个反灌的口子 |
| 3 | **固件必须配置 LDO_VO4** | `esp_ldo_acquire_channel({.chan_id=4, .voltage_mv=3300, .flags.adjustable=true})`, 位置在 `app_main` **最早期** (早于 display / SD / audio / IMU)。✅ 已落地: `vbcpet_main.c` 的 `ldo_vo4_init()` + `Kconfig.projbuild` 的 `CONFIG_VBCPET_LDO_VO4_MV` (默认 3300) |
| 4 | 其余引脚不动 | SD 卡 / PA_EN / MPU_INT / BL_PWM **全部沿用原引脚, 不需要搬脚** |
| 5 | 背光 PWM 保留 GPIO48 | 整组到 3.3V 后 MT3608 EN 可达 1.5V 阈值, **调光恢复**, 不必换到 GPIO49/50 |
| 6 | ⚠️ 一个 IDF 约束 | *"if the LDO channel is adjustable, you can't acquire it multiple times"* ⇒ 这条轨只在 `ldo_vo4_init()` 里 acquire 一次; **SD 驱动不要再用 `sd_pwr_ctrl_new_on_chip_ldo()` 碰通道 4** |

**启动顺序上的一个坑 (2026-09-25 踩到)**

上电自检跑在**任何 LDO 配置之前** ⇒ 只要 `ldo_vo4_init()` 还没进固件, 开机日志里 SD 必然
`sdmmc_card_init failed (0x105)`。这正是"手动拉压后功能好了、一重启又全废"的原因。
⇒ 已按上表第 3 项把配置挪进启动流程。

**功能验证结果 (2026-09-25, LDO_VO4 = 3300mV, 外部连线已拆)**

| 功能 | 结果 | 说明 |
|---|---|---|
| `audio` 自检 | ✅ **PASS** (`PA ON` → 440Hz×3 → PA off) | PA_EN = GPIO46, 改前只有 1.23V |
| DSI 显示 | ✅ `MIPI DSI backend ready` / `Boot screen displayed` | LDO_VO3 2500mV 供 DSI PHY, 与 VO4 互不干扰 |
| `test sd` (卡已插回) | ✅ **卡挂载成功** | 根目录列出 `ZIMAGE / ROOTFS~1.GZ / BOOT.SCR / MYD-Y6~1.DTB / MFG-IM~1 / SYSTEM~1` ⇒ **SD 功能恢复**。自检仍判 FAIL, 原因是这张卡是米尔 i.MX6 的系统盘, `open write FAIL` (只读), 与硬件无关 —— 换空卡即全绿 |
| `test imu` | ❌ FAIL `WHO_AM_I=0x00` | 与 I2C 总线故障同源, 见下 |
| `scan` | ❌ 0x03~0x77 **全部 FOUND 且 reg0=0x00** | 固件用的是**直读法**扫描 (不是 `i2c_master_probe`) ⇒ 不受 async 驱动限制 ⇒ 现象为真: **SDA 被恒久拉低**, 属硬件嫌疑 |

**固化验证 (2026-09-25 烧录 build_wifi 后)**

```
LDO_VO4: 已 acquire (用 ldo <chan> <mv> 改压)     ← 控制台回显, 证明 app_main 的 ldo_vo4_init() 已生效
```

旧固件此处会回 `未 acquire` (bringup 自己的 handle 是 NULL)。⇒ **启动期配置已生效, 不再依赖手敲**。

**4-bit SD** 仍失败在 `sdmmc_init_sd_ssr` (1-bit 正常)。1-bit 能挂载 ⇒ D0/CLK/CMD 都好,
嫌疑仍在 **DAT1~3 (GPIO40/41/42)** 走线: 断电蜂鸣档量 `M1.68↔TF1.8`、`M1.69↔TF1.1`、`M1.70↔TF1.2`。

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
