# V2 网表改动核对清单 (基准: 2026-09-15 版)

> 生成 2026-09-25 | 基准 `JLC/原理图_2026-09-15.enet` + `JLC/Netlist_PCB1_2026-09-15.enet`
> 用途: 在 EasyEDA 里逐项改, 改完对着这张表打勾。所有"现状"均为**从网表逐脚抽取**的结果, 不是记忆。
> 关联: [v2-execution-plan.md](v2-execution-plan.md) · [p4c6-manual-crosscheck.md](p4c6-manual-crosscheck.md) · [upgrade-roadmap-v2.md](upgrade-roadmap-v2.md)

---

## 0. 结论先行

**现在还不能开始布线。** 09-15 板是"改了一半"的版本:

- ✅ 电源保护 / USB-C 接 USJ / 舵机挪脚 / I2C 上拉 —— **已完成** (§1)
- 🔴 执行计划里的 **8 项改动一项都没落到网表** (§2)
- ⚠️ 另有 **3 个分支级未决**会改变拓扑, 不是补线能解决的 (§3) —— **其中 B1 已于 2026-09-25 关闭**, 余 2 项

> 原理图与 PCB 网表**逐网络比对完全一致** ⇒ 改完原理图后必须重新同步 PCB, 否则 PCB 还是旧的。

---

## 1. 已落地 —— 别动 (10 项)

| # | 项 | 网表证据 |
|---|---|---|
| 1 | USB-C = USJ (GPIO24/25) | `USB2.A6/B6(DP1/DP2) → USB_A_DP0 → M1.51`；`USB2.A7/B7(DN1/DN2) → USB_A_DM0 → M1.50` |
| 2 | USB-C CC 下拉 | `USB2.A5(CC1) → R6 5.1k → GND`；`USB2.B5(CC2) → R10 5.1k → GND` |
| 3 | USB-C 供电进 IP5306 | `USB2.A4B9/B4A9(VBUS) → VBUS_IN → SOC1.1(IP5306 VIN)` (带 C30 10µF) |
| 4 | 舵机让出 USJ | `M1.55 → SERVO1 (U10)`、`M1.56 → SERVO2 (U11)` 即 GPIO28/29 |
| 5 | 主 3V3 路径真保险 | `+5V → F2 (SMD0805-100-12, **1A PPTC**) → MP1.2(MP2315.IN)` |
| 6 | USB-A 保险 + ESD | `+5V → F1 (SMD0603-050-16, 0.5A PPTC) → USB1.VCC`；`D1 USBLC6-2SC6` (pin5 = **+5V** ✅) |
| 7 | 电源 TVS | `D3 PTVS3V3S1UR-MS` (3V3↔GND)、`D4 PTVS5V0S1UR-FS` (+5V↔GND) |
| 8 | I2C 上拉 | `R9 4.7k (3V3↔I2C_SCL)`、`R18 4.7k (3V3↔I2C_SDA)` —— **不是 R8**, 勿动 R8 |
| 9 | microSD 接线 | `M1.67-72 → SD_DAT0/1/2/3, SD_CLK, SD_CMD → TF1.7/8/1/2/5/3`；`M1.73 → SD_CD + R8 10k 上拉`；`TF1.4 → 3V3` |
| 10 | 模组基础脚 | `pin2 (LNA_OUT)` NC ✅；`pin84 (RTC_VBAT) → 3V3` ✅；`pin87 (CHIP_PU) → R11 10k + RST1` ✅ |

---

## 2. 待改 —— 8 项 (5 项无依赖, 可立刻做)

| # | 改什么 | 具体网络/位号 | 现状 | 依赖 |
|---|---|---|---|---|
| 1 | **`M1.76 (GPIO48)` 从 `3V3` 网摘除**；`U2.4 (MT3608 EN)` 从 `3V3` 改接回 `GPIO48` | `3V3` 网 43 个成员里同时有 `M1.76`、`U2.4`、`U2.5` | 🔴 未改。固件把 GPIO48 配成 LEDC ⇒ 每个 PWM 低电平段 3V3 经 GPIO48 对地短路；且背光常亮不可调 | **无** |
| 2 | ~~`pin66 ↔ +3V3` 加电阻位~~ → **pin66 保持 NC** (可选留一个测试点) | `M1.66 (VFB_VO4)` 当前就是 NC —— **保持 NC 即正确答案** | ✅ **已定论** (2026-09-25 实测): 该轨由 P4 内部 `LDO_VO4` 提供, 软件可完全支配 (1800/2700/3300 三档 pin66 与 GPIO48 同步跟随); 外部一接就是反灌一个输出脚。**改由固件配置**: `vbcpet_main.c: ldo_vo4_init()` + `CONFIG_VBCPET_LDO_VO4_MV` | §3-B1 **已关闭** |
| 3 | 新增 **`D5 = USBLC6-2SC6`**, 挂到 USJ 差分对 | `USB_A_DM0` / `USB_A_DP0` 两个网目前各只有 2 个成员 (M1 脚 + USB2 脚), 无保护 | 🔴 器件不存在 | **无** |
| 4 | 新增 **`R19 = 10k` 上拉**: `GPIO35 → 3V3` | `M1.62 → $1N248 → SW5 → GND` (只到开关) | 🔴 无上拉 | **无** |
| 5 | 新增 **`R20 = 10k` 上拉**: `GPIO36 → 3V3` (或**干脆不装**) | `M1.63 → $1N249 → SW6 → 3V3` (只到开关) | ⚠️ **方向已修正**: **绝不能下拉到 GND** —— Joint Download Boot 要求 `GPIO36=1`, 钉在 0 会让下载模式永远进不去。2026-09-25 实测**只用 GPIO35 接地即可进下载** ⇒ 浮空本来就够用, 上拉只是不依赖运气的保险 | **无** |
| 6 | C6 通道: `C6_U0RXD(pin4) ← GPIO31(pin58)`、`C6_U0TXD(pin5) → GPIO32(pin59)`、`C6_IO9(pin9) ← GPIO34(pin61)` + IO9 10k 上拉 3V3 | `M1.4/5/9` 全 NC；`M1.58/59/61` 也全 NC (空闲) | 🔴 未连 | §3-B2 |
| 7 | 删单脚悬空网 **`$1N152`** (`M1.48 = DM`), 该脚标 NC | `$1N152` 只有 `M1.48` 一个成员 | 🔴 还在 | **无** |
| 8 | 新增 **`D6 = SS34`** 做 `VBUS_IN → +5V` 单向 OR | 器件不存在 | 🔴 未加 | 需先确认 IP5306 边充边放行为 (无电池时 VOUT 实测 ≈1.9V) |

### 2.1 顺带的可维护性改动 (不改功能, 但强烈建议)

| 项 | 现在 | 建议 | 理由 |
|---|---|---|---|
| USJ 差分对网络名 | `USB_A_DM0` / `USB_A_DP0` | `USJ_DM` / `USJ_DP` | 现在和真 USB-A 的 `USB_A_DM`/`USB_A_DP` (GPIO26/27) **只差一个 0**, 布线/查错时极易看错 |
| strapping 网络名 | `$1N248` / `$1N249` | `STRAP_IO0` / `STRAP_IO1` | 自动命名网, 看网表时不知道是哪根 |

---

## 3. 分支级未决 —— 会改拓扑, 必须先定 (B1 已关闭, 余 2 项)

### B1. ~~`pin66` 到底能不能外部供 3.3V?~~ ✅ **已关闭 (2026-09-25 实测)**

手册原文: `Output power supply (output voltage range 0.5–2.7V or 3.3V, maximum output current 0.2A)`。
IDF `sd_pwr_ctrl_by_on_chip_ldo.h` 注释: *"set to `4` is the `LDO_VO4` is connected to power the SDMMC IO"*。

**实测 (拆掉 pin66 上的外部连线后, V1 板):**

| 命令 | pin66 | pin76 (GPIO48) |
|---|---|---|
| `ldo 4 1800` | 1.8 V | 1.8 V |
| `ldo 4 2700` | 2.7 V | 2.7 V |
| `ldo 4 3300` | 3.3 V | 3.3 V |

⇒ **通道 4 正确, 该轨完全由软件支配。外部供电完全不需要。**

- **pin66 = NC** (可选留一个测试点, 但**不要**留到 3V3 的电阻位)
- 改固件: `esp_ldo_acquire_channel({.chan_id=4, .voltage_mv=3300, .flags.adjustable=true})`,
  位置必须在 `app_main` 最早期 (早于 display / SD / audio / IMU) —— 否则上电自检必然失败
- ⚠️ `adjustable` 通道不能 acquire 两次 ⇒ **SD 驱动别再用 `sd_pwr_ctrl_new_on_chip_ldo()` 碰通道 4**

> 09-24 记的"软件改不动"是假象: 当天 `tools/serial_console.py` 的首字符丢失 bug 把 `ldo 4 3300`
> 吃成了 `?ldo`, 命令根本没到板子。该 bug 同一天已修。详见 [v2-execution-plan.md](v2-execution-plan.md) §A6。

### B2. C6 烧录通道留不留? (等原厂 + 待实测)

三根线本身好加 (`pin58/59/61` 空闲, 电平实测 3.24V ✅), 但**功能可行性从未验证过**:

- `pin5 (C6_U0TXD)` 实测 **0.12V** ⇒ C6 可能根本没在跑 (三种可能见 p4c6-manual-crosscheck.md §3.5b, 一种都没排除)
- 模组**没有引出 C6 的 EN** ⇒ 进 ROM 下载模式只能整板断电
- 若原厂答"出厂已预烧 esp-hosted slave + 支持 OTA" ⇒ **这 3 根线可以不走了** (零新增硬件)

**先问原厂再决定。** 这一项如果选"不走", 可以直接删掉, 不影响其他 7 项。

### B3. 4-bit SD 的 DAT1/DAT2/DAT3 到底通不通? (自己就能测, 2 分钟)

这是**唯一不用等任何人**的一项, 而且它决定 V2 的 SD 段走线要不要重点检查。

**现象**: pin66 直连 3.3V 后, 1-bit 挂载成功, 4-bit 仍失败在 `sdmmc_init_sd_ssr`。

**测法** (实物 V1 板, **断电**, 万用表蜂鸣档):

| 量 | 对应 | 通 ⇒ | 不通 ⇒ |
|---|---|---|---|
| `M1.68 ↔ TF1.8` | GPIO40 = DAT1 | 走线 OK | V1 走线断了 ⇒ V2 这段要重点检查 |
| `M1.69 ↔ TF1.1` | GPIO41 = DAT2 | 同上 | 同上 |
| `M1.70 ↔ TF1.2` | GPIO42 = DAT3 | 同上 | 同上 |

> 顺便: 这三根线都属 GPIO39~48 组, 量之前确认 pin66 的外部连线状态, 避免误判。

---

## 4. 改完后的自检

```powershell
# 新导出一份原理图 + PCB 网表, 然后:
python tools/enet_review.py "JLC/原理图_<新日期>.enet" --pcb "JLC/Netlist_PCB1_<新日期>.enet"
```

期望: **无 🔴**。重点看这几条:

- `C1` USJ 是否仍接 USB-C (改 D5 时容易碰掉)
- `C3` USJ 差分对是否有 ESD 保护 (= 第 3 项 `D5` 装了没)
- `C5` GPIO24/25 有没有被别的外设抢回去
- `C6` `+5V` / `VBUS_IN` 上的 PPTC / TVS 是否还在
- `C7` 悬空网络 —— 第 7 项删掉 `$1N152` 后这里应该干净

---

## 5. 推荐顺序

```
已完成 ⓪ LDO_VO4 定案 (B1 关闭) + 固件 ldo_vo4_init() 落地
今天  ① 量 B3 的 DAT1~3 通断 (2 分钟) + 把卡插回去复测 `test sd`
今天  ② 查 I2C: 断电量 I2C_SDA/SCL ↔ GND 电阻 (全 FOUND reg0=0x00 = SDA 被拉低)
同时  ③ 改 §2 的第 1/3/4/5/7 项 + §2.1 改名 (无依赖, 不等人)
等答复 ④ 发邮件问原厂 B2 (C6 预烧/OTA)
最后  ⑤ 定 §2 第 6 项 (C6 通道) + 第 8 项 (D6) → DRC → gerber
```
