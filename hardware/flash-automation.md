# 自动烧录方案 (USJ 即插即用) + 09-15 网表复核

> 2026-09-16 | 输入: `JLC/原理图_2026-09-15.enet` + `JLC/Netlist_PCB1_2026-09-15.enet`
> 复核工具: `tools/enet_review.py` (新) — `python tools/enet_review.py JLC/原理图_2026-09-15.enet --pcb JLC/Netlist_PCB1_2026-09-15.enet`
> 目标: **一条 USB-C 线 = 供电 + 烧录 + 串口日志 + (JTAG)**, 让 AI/无人值守能跑 编译→烧录→启动日志→自检, 不需要 strap 线、RST 按钮、CH340。

**决策记录 (2026-09-16)**
1. V2 打样: **先补齐 §3 的 3 处改动再打样** (ESD / GPIO36 上拉 / VBUS→+5V 单向 OR)。
2. V2 数据通道: **Live2D 推流一并搬到 USJ**, 一条 USB-C 全包 (烧录+日志+推流)。
3. 立即行动: `tools/ai_flash.py` ✅ 已落地 (见 §4.2)。

---

## 0. 结论 (TL;DR)

1. **09-15 原理图已经把 USJ (GPIO24/25) 引到板载 USB-C (USB2)**, 而 USJ 是 ESP32-P4 上**唯一能"由电脑软件复位进下载模式"**的口 → 自动烧录的硬件前提**已经具备**, 与 WT9932P4-TINY 的 J3「USB串口」口思路一致。
2. 还差 **3 处小改动** (见 §3): ① USJ 的 D± 缺 ESD ② GPIO36 缺上拉 (兜底下载模式电平不确定) ③ 无电池时 USB 5V 点不亮整板 (IP5306 无电池 VOUT≈1.9V)。
3. 软件侧要补的是: **按 VID:PID 认口**(不认 COM 号) + 一个 `tools/ai_flash.py` 把 编译/烧录/校验/抓日志/自检 串成**一条命令返回 JSON**。
4. **模组手册复核 (2026-09-17)**: `P4C6核心板说明书.pdf` 与 `ESP32-P4C6-Core核心板_使用指南260413.pdf` 是**同一个文件**(SHA256 相同); 手册 88 脚表与本板模组符号**逐脚 88/88 一致**。对照中揪出 **2 处必须打样前修的硬伤**(GPIO48 被接到 3V3、固件舵机脚还停在 V1) → 详见 [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md)。

---

## 1. 原理: 为什么 V1 麻烦、V2 能自动

ESP32-P4 有三个下载入口, 差别在**"电脑能不能自己把芯片弄进下载模式"**:

| 入口 | 引脚 | 电脑侧软件复位进下载 | 需要的外部电路 | 本项目 |
|---|---|---|---|---|
| **USB-Serial-JTAG (USJ)** | GPIO24 / GPIO25 | ✅ esptool 走 `USBJTAGSerialReset` (经 USB 写 `RTC_CNTL_FORCE_DOWNLOAD_BOOT`) | **无** (只需 CC 5.1k 下拉 + ESD) | **V2 已引到 USB-C** ✅ |
| USB 2.0 OTG HS | 模组 pin48 / pin49 | ❌ esptool 在 OTG 模式把 `hard_reset()` 退化成 `watchdog_reset()`, 只能重启动不能进下载 | 必须 **GPIO35=0 + GPIO36=1 硬 strap** | **V1 的 USB-C 走这里 → 麻烦的根因** |
| UART0 | GPIO37 / GPIO38 | 半自动 (需 DTR/RTS 两管自动复位电路) | CH340 + 两管电路, 或手按 | V1 用 CH340/飞线, 且控制台会僵死 |

依据: `esptool/targets/esp32p4.py` (`_post_connect`/`hard_reset`), 以及 ESP32-P4C6-Core 模组手册 §3.3 启动项:

- Strapping 脚 = **GPIO35/36/37/38**; 复位默认: **GPIO35 弱上拉=1**, GPIO36/37/38 浮空(模组内部无上拉)
- **SPI Boot** = GPIO35=1 (GPIO36/37/38 任意)
- **Joint Download Boot** = **GPIO35=0 + GPIO36=1**, 支持 USB-Serial-JTAG / USB2.0 OTG / UART / SPI Slave 四种下载方式
- ⇒ V1 现场"GPIO35→GND + GPIO36→3V3 + RST"正是手搓 Joint Download Boot; **USJ 通道下这一步由 esptool 全自动完成**

---

## 2. 09-15 网表复核结果

### ✅ 已经做对的

| 项 | 证据 |
|---|---|
| **USJ 引到板载 USB-C** | `M1.50/51 (GPIO24/25)` → `USB_A_DM0/DP0` → `USB2 (TYPE-C16PIN)` 的 A7/B7、A6/B6 |
| USB-C CC 下拉 | R6 / R10 = 5.1k 到 GND (A5/B5) — PC 会正常供 5V 并识别 |
| **舵机让位** | SERVO1/2 = GPIO28/29 (U10/U11), **GPIO24/25 已排他** |
| 电源保护补齐 | F2 = 1A PPTC (+5V→MP2315), D3 = 3.3V TVS, D4 = 5V TVS, C30/C16 = 10µF |
| V1 遗留疑点已修 | D1(USBLC6) pin5 = **+5V** (不再 VBUS_IN)；USB-A 口数据经 D1 保护 |
| I2C 上拉 | R9/R18 = 4.7k 到 3V3 |
| 原理图 ↔ PCB 网表一致 | 82 元件 / 81 网络逐脚比对 **完全一致** (PCB 已同步, 无残留旧网) |

### 🟡 建议在打样前改掉的 3 项

| # | 位置 | 现状 | 建议 | 理由 |
|---|---|---|---|---|
| 1 | USB2 的 D± (GPIO24/25) | **直连 USB-C, 无任何 ESD 器件** | 加 1 颗 USBLC6-2SC6 (或 3 通道 ESD 阵列) 到 GND | 这是**用户每次都要插拔**的口; TINY 板在同位置有 D1/D2/D3。USBLC6 的 0.8pF 对 USB FS 无影响 |
| 2 | GPIO36 (`$1N249`) | 只有 SW6「按住 → 3V3」, 松手即浮空 | **并 10k 上拉到 3V3** (SW6 可留作 SPI-slave 下载备用, 或删除); GPIO35 也建议并 10k 上拉 | 模组 GPIO36 **无内部上拉**, 浮空电平不确定; Joint Download Boot 要求 GPIO36=1。**V1 就是 4.7k strap 上拉开路导致整轮烧录失败 (08-24)** |
| 3 | VBUS_IN → +5V 之间 | 只有 IP5306 一条路 | 加 **SS34 (或 P-MOS 理想二极管) 从 VBUS_IN 到 +5V** | **无电池时 IP5306 VOUT 只有 ~1.9V**, 整板点不亮 → 插电脑线也烧不了 (V1 实测)。有电池时 IP5306 VOUT≈5.1V 反向截止, 互不干扰; MP2315 最低输入 4.5V, SS34 压降 ~0.4V 后 4.6V 仍可用 |

### ℹ️ 记录项 (不改也行)

- **F2 = 1A PPTC** 位置正确(+5V→MP2315), 但"屏亮 + 推流 + WiFi"满载时余量偏紧 → 建议 1.5A/2A, 或实测保持电流下的压降/温升。
- `M1.48 (USB2 OTG DM)` 是**单脚悬空网络 `$1N152`**, `M1.49 (DP)` 未连 → 标 NC 即可 (native USB OTG 本代不用)。
- 18 个 `$1Nxxx` 自动命名网络建议补 netlabel (`$1N248/249` = `BOOT_STRAP`/`DL_STRAP` 之类), 方便后续复核。
- **PCB 侧网表看不到、需在 EasyEDA 单独自检**: USB2 的 D± 是否 **90Ω 差分 / 等长 / 少过孔 / 不跨分割地**, USB-C 是否尽量靠近模组, ESD 器件是否贴口放置。

---

## 3. 硬件改动清单 (V2 冻结前)

| # | 位号 | 改动 | 封装/BOM |
|---|---|---|---|
| 1 | D5 (新) | USB2 D± ↔ GPIO24/25 加 ESD | USBLC6-2SC6 (SOT-23-6), 或 3 通道 ESD 阵列; VBUS 脚接 VBUS_IN |
| 2 | R19 / R20 (新) | GPIO36 / GPIO35 各 10k 上拉到 3V3 | 0603 10k; 与 SW5/SW6 并联 (SW5 保留作"按住进下载"兜底) |
| 3 | D6 (新) | VBUS_IN → +5V 单向 OR | SS34 (SMA) 或 P-MOS 理想二极管 (AO3401 + 电阻) |
| **4** | **M1.76 / U2.EN** | **断开 `M1.76(GPIO48) → 3V3`; 把 `U2(MT3608).EN` 从 3V3 摘下来接回 GPIO48** | 零成本改线。现状: 09-15 板把 `pin76(GPIO48)` 和 `U2.EN` 一起挂在 3V3 上, 而固件 `PIN_BL_PWM=48` 会把它按 PWM 拉低 ⇒ **3V3 经焊盘对地短路**; 顺便丢了背光调光。详见 [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) §3.1 |
| **5** | **C6 恢复通道** | `C6_U0RXD/U0TXD/IO9` (模组 pin4/5/9) → P4 `GPIO30/31/32`, IO9 加 10k 上拉 | 手册确认**模组没有引出 C6 的 EN**(唯一的 CHIP_PU 是 pin87 = P4 自己的) ⇒ C6 只能靠"IO9 拉低 + 整板断电"进下载。详见 [`../docs/wifi-camera-bringup.md`](../docs/wifi-camera-bringup.md) §5 |

---

## 4. 软件侧: AI 自动烧录工具链

### 4.1 固件配置 (关键 4 条)

1. `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` — 日志与烧录**同一根线**, 不再和推流抢 UART0。
2. **GPIO24/25 排他**: `board_config.h` 当前仍是 `PIN_SERVO_YAW=24 / PIN_SERVO_PITCH=25` → 改 **28/29**, 并禁止 `servo_init` 碰 24/25 (否则 LEDC 抢走 USB PHY, V1 已踩)。
3. 推流通道: 现阶段保留 UART0@2M 做过渡; **V2 目标是把 Live2D 推流也搬到 USJ** —— USB FS CDC bulk ≈ 1MB/s, 而 2M UART 只有 ~200KB/s, 这正是"42KB/帧 @2.5fps"的瓶颈; 协议不用改 (`channel_demux` 已是二进制自同步)。
4. 保留 `dl` 命令 (置 FORCE_DOWNLOAD_BOOT + `esp_restart()`) 作为**无壳/远程**兜底。

### 4.2 `tools/ai_flash.py` ✅ 已实现 (2026-09-16)

```bash
python tools/ai_flash.py --list-ports            # 列口 + VID:PID, 标出 USJ
python tools/ai_flash.py --build                 # idf.py build → 烧录 → 抓日志
python tools/ai_flash.py --monitor 12 --selftest # 烧完抓 12s 日志并下发 test all
python tools/ai_flash.py --info --json           # 只读芯片信息, JSON
python tools/ai_flash.py --dry-run               # 只打印将执行的命令
```

流程: 按 **VID:PID = `303A:1001`** 找口 (不认 COM 号) → `esptool --chip esp32p4 --before usb_reset --no-stub` 单进程 → `write_flash @build/flash_args` → hash 校验 → 复位 → 抓 N 秒启动日志 → 可选下发 `test all` → **输出 JSON** `{ok, port, chip, flash:{sha256}, boot:{boot_ok,crashes}, selftest}`, 退出码 0/1/2。

细节: esptool 自动从 `firmware/_idf.py` 里解析出的 IDF python env 找 (无需 activate); `--kill-stale` 清掉占口的 streamer/monitor 残留进程; 日志口可单独指定 (`--monitor-port`), 照顾"控制台还在 UART0"的过渡期。

替代现在的人工流程 (`tools/flash-v1/flash_osptek.ps1` / `tools/flash-v1/sync_flash3.ps1`: 清残留进程 → 夹 strap → 点 RST → 单进程烧 ≥3 次 → 拔 strap), 全部省掉。端口混用问题 (COM16/COM22/COM8/COM10) 也由 VID:PID 认口根治。

### 4.3 无人值守流水线

`build` → `ai_flash` → 断言启动日志 (`boot:0x??` / PSRAM 32MB / `test all` 逐项 PASS) → 失败自动重试/回滚上一镜像。写进 `firmware/README.md`。

---

## 5. V1 板应急飞线 (不用等 V2, 今天就能验证)

V1 的舵机排针 **U8 pin3 = GPIO24 (USJ_D-)**、**U9 pin3 = GPIO25 (USJ_D+)** —— 从这两脚 + 任一 GND 飞 **3 根线**到 USB-C 母座转接板 (数据线 A6/A7+B6/B7 并联, CC 可悬空, VBUS 不接):

1. **最后一次人工**: GPIO35→GND + GPIO36→3V3 strap + RST1 进下载, 烧入"**GPIO24/25 排他 + USB-Serial-JTAG 控制台**"的固件。
2. **之后全自动**: 一根线 → `ai_flash.py` (esptool 直接 `usb_reset`), 无 strap、无 RST、无 CH340。
3. 若某次刷成砖 (固件又占了 24/25), 退回 strap 兜底即可 —— 所以 SW5/SW6 + R19/R20 要在 V2 上保留。

---

## 6. 验收清单 (V2 到手逐条打勾)

- [ ] 插 USB-C 到电脑: 设备管理器出现 `USB 串行设备 (COMx)`, VID:PID = 303A:1001
- [ ] `esptool.py --chip esp32p4 chip_id` 不按任何按键即可读到芯片 (自动进下载)
- [ ] `ai_flash.py` 一条命令: 烧录 + hash 校验 + 抓日志 + `test all` 全绿
- [ ] 断电重启后仍能自动烧录 (不依赖上次固件状态)
- [ ] 无电池、只插 USB-C: 3V3 轨正常, 板子能点亮
- [ ] 按住 SW5 + 点 RST1: 仍能进下载 (兜底通道有效)
- [ ] USB-A 外设口 (GPIO26/27) 与 USJ 口互不干扰: 同时插两个设备都能枚举

---

## 7. V2: 下载还需要 strap 吗? (2026-09-16 明确)

**结论: P4 不需要; C6 正常也不需要。但兜底按键要留 (是按键, 不是飞线)。**

| 目标 | 需要 strap? | 依据 |
|---|---|---|
| **P4 (V2)** | ❌ **不需要** | V2 把 USB-C 接到 **GPIO24/25 = USB-Serial-JTAG**; esptool 走 `--before usb_reset` (写 `RTC_CNTL_FORCE_DOWNLOAD_BOOT`) 就能把芯片弄进下载模式。V1 麻烦是因为它的 USB-C 接的是 native USB OTG (pin48/49), 没有这条软件通路 |
| **C6 (正常升级)** | ❌ 不需要 | 走 esp-hosted 的 SDIO OTA (`cpota`), C6 自己重启 |
| **C6 (空片)** | ❌ 不需要 (待实测确认) | ROM 没有有效镜像时会停在 UART 下载模式 → P4 直接烧 |
| **C6 (坏/不兼容固件)** | ⚠️ 需要"拉低 `C6_IO9` + 断电重上电" | **手册已确认这条路是唯一的路**: 模组 88 脚表里没有 C6 EN([`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) §1/§6); GPIO54 只是模组 pin82 上一个普通 P4 GPIO, 没有任何证据指向 C6 EN ⇒ "纯软件复位 C6"这条在 V2 上不成立, 除非鱼鹰书面确认 |

### 7.1 让"P4 免 strap"成立的前提 (缺一条就打回原形)

1. **GPIO24/25 排他**: 固件任何地方都不得再配置这两个脚 (V2 已把舵机挪到 GPIO28/29; `board_config.h` + 代码审查要守住)。
2. USB-C 有 **CC1/CC2 5.1k 下拉** ✅ (09-15 网表已是 R6/R10)。
3. USB-C 的 D± 按 **90Ω 差分**布线 (待 PCB 侧自检), 并 **加 ESD** (目前缺, 见 §3)。
4. **无电池也能上电**: VBUS_IN → +5V 加单向 OR (SS34/理想二极管), 否则插电脑线板子不亮 (IP5306 无电池 VOUT≈1.9V)。
5. 固件不把 USB-Serial-JTAG 关掉/抢走 (例如控制台与推流复用同一条 USJ 时不能打架)。

### 7.2 什么时候还得靠兜底 (所以要留按键 + 两个 10k)

| 情形 | 兜底动作 |
|---|---|
| 固件把 GPIO24/25 抢走 / 挂死在 app 里 | 按住 `SW5` (GPIO35→GND) + 点 `RST1` → 进 Joint Download Boot |
| USJ 相关外设被固件关掉、PHY 不枚举 | 同上, 或断电重上电再试 |
| eFuse 误烧 (禁止下载模式) | 不可恢复, 只能换模组 |

⇒ V2 保留 `SW5` / `RST1`; `SW6` 可删, 改为 **GPIO36 加 10k 上拉** (Joint Download Boot 需要 GPIO36=1, 这样只按 SW5 就够); **GPIO35 也加 10k 上拉** (芯片复位时已有弱上拉, 但 V1 就是这颗 4.7k 上拉开路废掉一整轮烧录)。
