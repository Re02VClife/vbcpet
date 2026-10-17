# 摄像头 OV5640 @ CN1 — 验证准备（2026-09-28）

> 本文记录 **2026-09-28 实测/查证到的硬事实** + 固件侧新增的探测路径。
> 上位文档: [`bringup-v1-status.md`](bringup-v1-status.md) §5 · [`../docs/wifi-camera-bringup.md`](../docs/wifi-camera-bringup.md) 二节。

---

## 1. 板上实测（今天在 COM23 上跑出来的）

| 命令 | 结果 | 读法 |
|---|---|---|
| `scan` | `FOUND=2 NACK=115 TIMEOUT=0`，命中 `0x28`、`0x55` | 总线健康；I2C 修复（`trans_queue_depth=0`）在跑的是新固件 |
| `ldo 4` | `已 acquire` | 启动期 `ldo_vo4_init()` 生效 ⇒ 板上跑的就是当前 `main` |
| `gp 13l` / `gp 12h` 后 `scan` | **仍无 `0x3C`** | ⚠️ 见 §3：这**不能**证明模块坏 |
| `rd 3c 0a` | 回 `用法: rd <addr_hex> <reg_hex>` | **`rd` 命令本身有 bug**，今天已修（见 §4） |

> 时间戳 `I (2709080)` ⇒ 板子已连续运行约 45 分钟（不是刚复位）。

## 2. 网表复核（从 `JLC/原理图_2026-09-15.enet` 解析出来）

`CN1 = KH-FG0.5-H2.0-24PIN`（24pin 0.5mm FPC），逐脚：

| CN1 | 网络 | CN1 | 网络 |
|---|---|---|---|
| 1 | NC | 13 | CSI_D1N |
| 2 | GND | 14 | **CAM_MCLK** |
| 3 | I2C_SDA | 15 | GND |
| 4 | +2V8 | 16 | CSI_CLKP |
| 5 | I2C_SCL | 17 | NC |
| 6 | **CAM_RSTB** | 18 | CSI_CLKN |
| 7 | NC | 19 | NC |
| 8 | **CAM_PWDN** | 20 | CSI_D0P |
| 9 | NC | 21 | NC |
| 10 | +1.5V | 22 | CSI_D0N |
| 11 | +2V8 | 23 | +2V8 |
| 12 | CSI_D1P | 24 | GND（25/26 也是 GND） |

模组侧：`M1.20→CAM_MCLK`、`M1.27→CAM_RSTB`、`M1.28→CAM_PWDN`、
`M1.41/42→CSI_D0N/D0P`、`M1.43/44→CSI_CLKP/CLKN`、`M1.45/46→CSI_D1N/D1P`。

⚠️ **两条纠错**（相比旧文档）：

1. `bringup-v1-status.md` §5 把 `CAM_MCLK` 记成 "pin20"、`CAM_RSTB` "pin27"、
   `CAM_PWDN` "pin28" —— 那是 **M1（模组）的脚号**，不是 CN1 的。CN1 上是
   **14 / 6 / 8**。
2. **24pin 模块的资料已不在仓库里**：原先根目录那份 `FYRK3399-OV5640-100FPC规格书`
   是 **30pin** 版本（1 DVDD / 2 GND / 3 DVDD 1.5V / 4 DOVDD 2.8V / 7 AVDD-2V8 /
   9 SDA / 10 SCL / 11 RST / 12 PWDN / 13 GND / 14 MCLK / 25 MCP / 26 MCN /
   28 DP0 / 29 DN0 / 30 GND），2026-08-04 已被 24pin 方案**取代**
   （见 `expansion.md:51`、`WORKLOG.md:963`）。**2026-09-28 按用户要求删除该 PDF**，
   因为它只会误导（30pin 表对 24pin 座全错位；且 30pin 排线物理上插不进 24pin 座，
   硬塞会错位约 6 个触点）。**24pin 的权威依据 = 网表里 CN1 的逐脚连接（§2 表）。**

   ✅ **2026-09-29 补上**：已拿到 **24pin 模块规格书**并归档为
   [`datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf`](datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf)
   （仁恒影像，584612 bytes，sha256 `DDC2AC5B…628B19D`）。其引脚表与 §2 网表**逐脚一致**
   —— 见 §3.6.11。

## 3. 为什么"扫不到 0x3C"不成立为判据

三条独立原因，每条都足以让 OV5640 在这条总线上沉默：

1. **RSTB / PWDN 无上下拉**。网表里 `CAM_RSTB`、`CAM_PWDN` 这两条网**只有 M1 和 CN1
   两个脚**（脚本扫过全表，没有任何 R 到 3V3/GND）。复位后 P4 侧是浮空输入 ⇒
   OV5640 停在未定义态，**RSTB 低有效**时就是被按在复位里。
2. **没有 XVCLK**。`GPIO5` 复位后不是时钟输出，OV5640 没有 24MHz 不会应答 SCCB。
   （`gp` 命令只能置静态电平，做不出 24MHz。）
3. **SCCB 读寄存器是 16 位地址**（0x300A）。`scan` 用的是"读 reg 0x00"的 8 位直读法，
   对 OV5640 本来就不是有效事务。修复后的 `rd`/SCCB 路径要用 2 字节地址。

另外 OV5640 的 PWDN 是**高有效**（`OV5640_PWDN_ACTIVE 1`、`RST_ACTIVE 0`，
依据官方 HPM SDK `hpm_ov5640.h`）。但本板文档一直标着 `PWDNB 极性待确认`，所以固件里
两种极性都能扫（`cam pol`），不靠假设。

---

## 3.5 首轮实测（2026-09-28 当天跑完）—— ⚠️ 见 §3.6，本节结论**已部分撤回**

固件侧三条嫌疑**全部排除**，硬件侧定位到"CN1 ↔ 模块之间不通"：

| 检查项 | 结果 | 判据 |
|---|---|---|
| I2C 总线 | ✅ 正常 | `0x28`(reg0=0xFC) 与 `0x55` 都能 `rd` 成功 |
| MCLK 24MHz | ✅ 起振 | `cam: MCLK: GPIO5 ← LEDC, 24MHz` |
| 复位/PWDN 时序 | ✅ 已按内核驱动重写 | `gp 12r`/`gp 13r` 实测两个脚都动 |
| 上电时序 | ✅ 正确 | RSTB 摁住 → PWDN 掉电 → 8ms → 上电 → 8ms → 释放 → 2ms → 常态低 → 25ms |
| `i2c_master_probe(0x3C)` | ❌ **ESP_ERR_NOT_FOUND(261)** | 从机 **NACK**：地址上没有任何器件 |
| 地址 0x3C/0x3D/0x3E | ❌ 全部 NACK | |
| `CN1.4/11/23` (+2V8) | ✅ 用户实测 2.8V | LDO1 正常 |
| `CN1.10` (+1V5) | ✅ 用户实测 1.5V | LDO2 正常 |
| **`CN1.3` / `CN1.5`（SDA/SCL）** | ⚠️ 都是 3.3V | 板上 R18/R9 4.7k 上拉到 3V3 的电压 ⇒ 这两条线上没有从机在钳位 |
| 座子↔模块 24 针通断 | ✅ **24 针全通** | 用户断电蜂鸣档实测（排线直连摄像头） |
| 模块是否上电 | ✅ 轻微发热 | 用户手感；符合"核心已上电" |
| `cam raw`（不碰引脚，复位释放后**等 1s** 再发事务） | ❌ `probe(0x3C)=NOT_FOUND(261)` | **排除"复位释放后等待不够"**：1s 远超内核的 20ms |
| `cam pol`（PWDN 高/低 极性 × 0x3C/0x3D/0x3E = 6 组合） | ❌ **6 个组合全部 NOT_FOUND** | **排除"PWDN 极性反了"** —— `board_config.h` 那句 `PWDNB 极性待确认` 可以结案：两种都不应答 |
| `scan`（换地址找） | ❌ 只有 `0x28` + `0x55` | 摄像头**在任何地址都不出现** |

⇒ **软件嫌疑 100% 清零**。模块通电、24 针全通、同一总线其他器件正常，但模块在 I2C 上
**完全不响应** ⇒ **判定为模块（或其 I2C 接口）故障**。发热也符合"上电但内部没正常工作"。

**交接结论：换模块。** 换到新的 OV5640 24pin 后，直接跑
`test cam`（或 `cam`）——预期 `PID: 0x300A=0x56 0x300B=0x40`。固件侧无需再改。
若换模块仍 NACK，再回头审板（CN1 焊盘/封装）或确认该模块的 24pin 定义与本板是否一致。

---

## 3.6 ⚠️ 2026-09-29 复测：结论部分撤回 + 找到一处真 bug

### 3.6.1 必须先撤回的一句话

上节那句 **"软件嫌疑 100% 清零" 是错的**，因为当时没发现 `cam_power_sequence()`
里的复位 bug（见 3.6.3）。**`test cam` / `cam pol` 这两条证据在 09-28 是无效的**——
它们测的是"一个被一直摁在复位里的模块"。真正有效的只有 `cam raw`（手动放开复位）。

### 3.6.2 重插排线后：结果逐字一致

用户把模块/排线**重新插拔**后复测，与 09-28 **逐字节相同**：

| 命令 | 09-28 | 09-29 重插后 |
|---|---|---|
| `test cam` | `probe(0x3C)=NOT_FOUND(261)` | 同 |
| `cam raw`（放开复位等 1s） | `NOT_FOUND(261)` | 同 |
| `cam pol` (6 组合) | 全 `NOT_FOUND(261)` | 同 |
| `scan` | `FOUND=2 NACK=115`（只有 0x28/0x55） | 同 |

⇒ **接触不良不是原因**，重插无效。

### 3.6.3 🐞 复位时序 bug（真 bug，已修）

`camera.c` 的 `cam_power_sequence()` 结尾原本是：

```c
gpio_set_level(PIN_CAM_RSTB, 1);   /* 释放复位 */
vTaskDelay(pdMS_TO_TICKS(2));
gpio_set_level(PIN_CAM_RSTB, 0);   /* RESETB 常态低 */   ← 把复位又摁回去了
```

这是把 Linux 内核 `gpiod_set_value()` 的**逻辑值**当**物理电平**抄了。内核的
`set_value(0)` 语义是 "deassert = 释放复位"，物理电平由 DT 的 `GPIO_ACTIVE_LOW` 决定；
本板 `REST` **低有效**（`expansion.md:79`）⇒ 释放复位 = **物理高**。
所以原实现在函数返回时把模块**一直摁在复位里**，之后所有探测必然 NACK。

**修法**：删掉最后那次拉低，RSTB 停在 1（释放）并保持。

> 注意：这**不能**解释全部现象 —— `cam raw` 走的是"手动 `gp 12h` 放开复位再等 1s"，
> 不经过这个 bug，却同样 NACK。所以它是真 bug，但不是唯一嫌疑。

### 3.6.4 扫了 RSTB 极性 × PWDN 极性 4 组合 —— 全部 NACK

用现有 `gp` 命令手动走完整时序（`gp 13h` 掉电 → `gp 12l` 断言 → `gp 13l` 上电 →
`gp 12h` 释放 → 等 0.4s → `cam raw`），4 组合结果：

| RSTB 最终电平 | PWDN 工作电平 | `cam raw` |
|---|---|---|
| 高（释放） | 低 | `NOT_FOUND(261)`，141µs / 171µs / 169µs |
| 低 | 低 | 同上，**逐字相同** |
| 高 | 高 | 同上 |
| 低 | 高 | 同上 |

**耗时逐字相同**（141µs / 171µs / 169µs）说明模块对这两个脚的任何组合都**毫无反应**
——不像"极性选错"，更像模块在这些线路上没有电气存在感。

### 3.6.5 唯一"驱动说 OK、却从没人看过"的环节：MCLK

`cam_mclk_start()` 里只有 `ledc_timer_config()` / `ledc_channel_config()` 的**返回值**，
**从来没有实测过 GPIO5 上是否真的有 24MHz**。而
**OV5640 没有 XVCLK(XCLK) 就完全不应答 SCCB** —— 这恰好就是观察到的现象。

⇒ 固件新增 **`cam mclk`**：用 **PCNT** 在 GPIO5 上数脉冲反推真实频率（GPIO 输出与
PCNT 输入可共用同一 pad）。这是唯一能"软件自证 MCLK 真的输出"的手段，否则只能靠
万用表/示波器量 CN1.14。

> ⚠️ 实现上**故意不调用** `pcnt_unit_set_glitch_filter()`：默认 glitch 滤波阈值在
> 1000ns 量级，会把 24MHz（周期 41ns）整个滤掉，读回 0 个脉冲，反而"证实"一个
> 不存在的故障。

### 3.6.6 已排除：DVP/MIPI 模块混淆

`expansion.md:52-53` 记过"手上 ESP32-S3-CAM 的 **DVP 24pin 模块不能接 P4 MIPI-CSI**，
弃用"。DVP 与 MIPI 的 OV5640 都是 24pin 0.5mm，排线一样、插得进去，但 pinout 是两套
定义——所以这条本来是**很强的嫌疑**（能解释"发热但不应答"）。

**2026-09-29 已向用户确认：插在 CN1 上的是 08-03 采购的 MIPI 版。** 该嫌疑排除。

### 3.6.7 🔴 MCLK 实测结果：GPIO5 上**数不到任何脉冲**（2026-09-29）

烧入带 `cam mclk` 的固件后实测（PCNT 在 GPIO5 上数脉冲）：

```
cam: MCLK 实测: GPIO5 在 265us 内数到 0 个脉冲 ⇒ ≈0 Hz  [≈0 —— MCLK 没有真正输出到 pad!]
cam: MCLK 实测: GPIO5 在 731us 内数到 0 个脉冲 ⇒ ≈0 Hz  [≈0 —— MCLK 没有真正输出到 pad!]
cam: MCLK 实测: GPIO5 在 735us 内数到 0 个脉冲 ⇒ ≈0 Hz  [≈0 —— MCLK 没有真正输出到 pad!]
```

**排除"LEDC 绑定丢失"**：先 `gp 5r` 把 pad 抢成输入（毁掉 LEDC 输出），再 `cam mclk`
—— `cam_mclk_start()` 重新绑定之后**仍然 0**。

**GPIO5 的 pad 本身电气正常**：

| 操作 | 读回 | 含义 |
|---|---|---|
| `gp 5h` → `gp 5r` | **1** | 能驱动到高 ⇒ **没有对地短路** |
| `gp 5l` → `gp 5r` | **0** | 能驱动到低 |
| `gp 12h`/`12l` → `gp 12r`（对照） | 1 / 0 | RSTB 同样正常 |

**复位 bug 修好后 `test cam` 仍然 `NOT_FOUND(261)`** ⇒ 复位时序也不是根因。

⇒ **新的首要嫌疑：`ledc_channel_config()` 返回 `ESP_OK`，但 LEDC 的信号根本没有送到
GPIO5。** 而 **OV5640 没有 XVCLK 就完全不应答 SCCB** —— 这与全部现象吻合。
**结论从"模块故障"转向"板上 MCLK 没有输出"。**

⚠️ **必须先交叉验证（PCNT 测量通路本身也可能不可信）**：
用万用表量 **CN1.14（或模组 M1.pin20）在 LEDC 运行时的直流电压**：

| 读数 | 含义 | 下一步 |
|---|---|---|
| ≈1.6V | LEDC 有输出（24MHz 50% 方波的平均值）⇒ **PCNT 测法有问题** | 回头查 PCNT 输入通路 |
| 0V | LEDC 确实没输出 | 查 LEDC 配置：时钟源 `LEDC_USE_PLL_DIV_CLK` 在 P4 上是否有效、`LEDC_TIMER_1_BIT`@24MHz 分频是否合法、P4 上 GPIO5 能否做 LEDC 输出 |
| 3.3V | LEDC 输出卡在高电平 | 同上 |

### 3.6.8 ✅ 板上到 CN1 的**每一条信号链路**都验证通过（2026-09-29）

用"**慢速方波 + 万用表看读数是否跟随跳动**"逐个验证 —— 比静态量电平更能证明连通
（断路时会被上拉钳在 3.3V 不动）：

| 信号 | CN1 脚 | 方法 | 结果 |
|---|---|---|---|
| CAM_MCLK | 14 | `gp 5h` 后量直流 | ✅ **3.3V** ⇒ 走线通，且**电平域是 3.3V**（不是 1.8V） |
| CAM_MCLK | 14 | LEDC 运行时量直流 | ✅ **0.75V**（既非 0V 也非 3.3V）⇒ **在翻转，LEDC 确实有输出** |
| CAM_RSTB | 6 | 2 秒周期翻转，量是否跟随 | ✅ 跟随 **0↔3.3V** |
| CAM_PWDN | 8 | 2 秒周期翻转，量是否跟随 | ✅ 跟随 **0↔3.3V** |
| I2C_SDA | 3 | 2 秒周期翻转，量是否跟随 | ✅ 跟随 **0↔3.3V** |
| I2C_SCL | 5 | 2 秒周期翻转，量是否跟随 | ✅ 跟随 **0↔3.3V** |
| +2V8 / +1V5 | 4,11,23 / 10 | 直流 | ✅ 2.8V / 1.5V（08-17 已测） |

> ⚠️ 这同时**推翻了上面 3.6.7 里"LEDC 没输出"的怀疑**：0.75V 是 24MHz 方波在万用表
> 直流档上的平均值（高频下读数低于理论半幅 1.65V）。**MCLK 是好的。**

> 🐞 **`cam mclk` 为什么会误报 0 个脉冲**：`ledc_channel_config()` 把 pad 设成
> `GPIO_MODE_OUTPUT` 时会**关掉输入缓冲**（`gpio_ll_input_disable`），而 PCNT 走的是
> GPIO matrix 的**输入**路径 ⇒ 数到 0。**测量方法本身破坏了被测信号。**
> 修法：PCNT 配好之后补一句 `gpio_input_enable(PIN_CAM_MCLK)`（只开输入缓冲、不动输出）。
> **待下次烧录验证。**

### 3.6.9 剩余嫌疑（板上已全部排除之后）

板上链路 / 电源 / MCLK / 电平域 / 走线 / 焊盘**全部正常**，所以只剩三处可能：

| # | 嫌疑 | 怎么验 |
|---|---|---|
| 1 | **模块的 24pin 定义与 CN1 网表不符** | 需要模块型号 / 规格书比对。**注**：若电源脚错位，会把 GND 接到 +2V8 上造成大电流；而实测 +2V8/+1V5 稳定、模块只是"轻微发热" ⇒ **电源部分对位是对的** |
| 2 | **CN1 座子触点 ↔ 排线金手指的接触** | 首轮那句"24 针全通"测的是**排线两端**，不含座子接触；需在排线插入状态下从模块侧量 |
| 3 | **模块本体故障** | 换件 |

### 3.6.10 ⚠️ 「24pin MIPI OV5640」**没有统一引脚标准**

查证（2026-09-29）：市面上的 24pin MIPI OV5640 模组**各家用各自的定义**，差异是**整条排线
级别**的，不是细节。以 Toradex《CSI Camera Module 5MP OV5640》为例：

| Pin | Toradex | Pin | Toradex |
|---|---|---|---|
| 1 | GND | 13 | I2C_CAM1_SCL |
| 2 | CSI_D0_N | 14 | I2C_CAM1_SDA |
| 3 | CSI_D0_P | 15 | **+3.3V** |
| 4 | GND | 16 | NC |
| 5 | CSI_D1_N | 17 | NC |
| 6 | CSI_D1_P | 18 | GND |
| 7 | GND | 19 | NC |
| 8 | CSI_CLK_N | 20 | NC |
| 9 | CSI_CLK_P | 21 | **+5V** |
| 10 | GND | 22 | CSI_GPIO1 (PWDN) |
| 11 | CSI_GPIO0_RST | 23 | CSI_GPIO2 |
| 12 | CAM1_MCLK | 24 | CSI_GPIO3 |

对比本板 CN1（§2 表）：

| 信号 | 本板 CN1 | Toradex |
|---|---|---|
| SDA / SCL | **3 / 5** | 13 / 14 |
| MCLK | **14** | 12 |
| RST | **6** | 11 |
| PWDN | **8** | 22 |
| 供电 | **+2V8 (4/11/23) + +1V5 (10) 直供传感器** | **+3.3V (15) + +5V (21)，板上自己稳压** |

⇒ **"买的是 MIPI 版"只说明接口类型对，不能保证引脚顺序与本板匹配。**
本板 CN1 是照**某一款特定模组**（`REST` / `PWDNB` / `AF-VCC` / `STROBE` / `AGND` /
`AF-GND` 这套信号名）设计的，而那款模组的规格书**目前不在仓库里**
（根目录那份是 30pin 版，2026-09-28 已删）。

> **一个有用的间接判据**：若模块 pinout 与本板严重不符，模块的 GND 很可能撞上 CN1 的
> `+2V8`/`+1V5`（本板电源脚在 4/11/23/10），造成**大电流 / 电源被拉低**。
> 实测 +2V8/+1V5 稳定、模块只是"轻微发热" ⇒ **电源那几脚至少是对上的**，
> 但这**不等于** SDA/SCL/MCLK 也对上。
>
> ⚠️ 另注：`expansion.md` §446 那句"已按规格书核对"写于 **2026-08-04**，
> 而 24pin 方案是 **08-05** 才定的 —— 那个"规格书"很可能指的是后来被弃用的
> **30pin（FYRK3399）版本**，不能当作 24pin 已经核对过的证据。

### 3.6.11 ✅ 拿到 24pin 规格书：引脚定义与本板 CN1 **逐脚一致**

用户提供了 24pin 模块的规格书（`OV5640-MIPI接口 78MM 摄像头23AF 仁恒 2024年3月29日.pdf`，
深圳市仁恒影像科技）。**单页矢量图，无文本层、无位图** ⇒ 必须渲染成 PNG 再读图
（脚本：`tools/_render_renheng.py`、`tools/_crop_renheng.py`；PDF 本身含中文文件名，
要先复制到 ASCII 路径，否则 `pymupdf` 报 `FileNotFoundError`）。

规格书 **MIPI 接口** 引脚表（1 → 24）：

| PIN | NAME | PIN | NAME | PIN | NAME |
|---|---|---|---|---|---|
| 1 | STROBE | 9 | NC | 17 | NC |
| 2 | AGND | 10 | **DVDD** | 18 | MCN |
| 3 | **SDA** | 11 | **DOVDD** | 19 | NC |
| 4 | **AVDD** | 12 | MDP1 | 20 | MDP0 |
| 5 | **SCL** | 13 | MDN1 | 21 | NC |
| 6 | **REST** | 14 | **MCLK** | 22 | MDN0 |
| 7 | NC | 15 | **GND** | 23 | AF-VCC |
| 8 | **PWDN** | 16 | MCP | 24 | AF-GND |

对照本板网表（§2）与 [`expansion.md`](../expansion.md):70-85 —— **逐脚一致**：

- 控制：`SDA=3` / `SCL=5` / `REST=6` / `PWDN=8` / `MCLK=14` / `STROBE=1`
- 电源：`DVDD=10 (+1V5)` / `DOVDD=11 (+2V8)` / `AVDD=4 (+2V8)` / `AF-VCC=23 (+2V8)`
- 地：`GND=2,15,24`（规格书标 AGND/GND/AF-GND）；`NC=7,9,17,19,21`
- 差分：`MDP1/MDN1=12/13`、`MCP/MCN=16/18`、`MDP0/MDN0=20/22`

**电源与光学参数也一致**：AVDD 2.8V / DVDD 1.5V / DOVDD 2.8V；500W、4P+IR、
EFL 3.2、F/No 2.6、FOV 84°。
（规格书写 `PWDN`，与 OV5640 数据手册一致 = **高有效**，即固件默认的
`s_pwdn_active_high=1`，工作电平为低。）

⇒ **「模块的 24pin 定义与本板不符」这条嫌疑排除。板子的设计是对的。**

**于是只剩两处**：

| # | 嫌疑 | 怎么验 |
|---|---|---|
| 1 | **CN1 座子触点 ↔ 排线金手指的接触** | 首轮那句"24 针全通"测的是**排线两端**，不含座子接触；需在排线插入状态下从模块侧量 |
| 2 | **模块本体故障** | 换件 |






## 4. 固件改动（今天已编译进 `build_wifi/vbcpet.bin`）

| 文件 | 改动 |
|---|---|
| `firmware/main/peripherals/camera.c` / `camera.h`（新） | `camera_probe()`：LEDC 出 24MHz MCLK（GPIO5）→ 按内核驱动的 PWDN/RSTB 时序 → `i2c_master_probe(0x3C)` 判定 ACK/NACK → SCCB 读 `0x300A/0x300B`（期望 `0x56/0x40`） |
| 同上 | `cam [addr\|pol\|raw]` 控制台命令：现场重跑探测 / 扫 PWDN 极性 × 地址（`pol`）/ 不碰引脚只 probe（`raw`） |
| 同上（**09-29 新增**） | **修复位时序 bug**（见 §3.6.3：结束后曾把 RESTB 停在复位断言） |
| 同上（**09-29 新增**） | **`cam mclk`**：PCNT 在 GPIO5 上实测脉冲数 ⇒ 真实频率（见 §3.6.5） |
| 同上（**09-29 新增**） | `cam_mclk_start()` 去掉"已启动就早退"（`gp 5x` / PCNT 抢走 pad 后 MCLK 再也回不来） |
| `bringup.c` | `bringup_init()` 里调用 `camera_probe()`（失败只告警）；**修 `rd` 命令 bug** |
| `self_test.c` | 注册 `test cam`（连读 3 次，顺带暴露接触不良） |
| `CMakeLists.txt` | 编入 `peripherals/camera.c` |

**踩过的两个坑（都已修）**

1. **LEDC 时钟源是全局的**：P4 上所有 LEDC timer 必须同源。背光与舵机原用 `LEDC_AUTO_CLK`，
   驱动替它们选了 RC_FAST(~20MHz)，摄像头要 80MHz 出 24MHz 时被拒：
   `E ledc: ledc_set_timer_div(693): timer clock conflict, already is 18 but attempt to 9`。
   ⇒ 背光/舵机/摄像头**全部显式钉 `LEDC_USE_PLL_DIV_CLK`**；摄像头挪到 `TIMER_2/CHANNEL_3`。
2. **共享 I2C 必须先 add_device**：只 `i2c_bus_init()` 不 `i2c_bus_add_device()`，`s_dev` 是 `NULL`，
   `i2c_master_transmit_receive(NULL,...)` 秒回 `ESP_ERR_INVALID_STATE(259)`（几十微秒，
   真超时是 100ms 级），看起来像"总线故障"。

**复位时序参照**（Linux 内核 `drivers/media/i2c/ov5640.c` 的 `ov5640_reset()`）：
`RESETB=0 → PWDN=掉电 → 5~10ms → PWDN=上电(RESETB 仍为 0) → 5~10ms → RESETB=1 → 1~2ms
→ RESETB=0(常态) → 20~25ms 后才谈 SCCB`。第一版实现是"先上电再复位"，顺序反了。

`rd` 的 bug：原来只 `sscanf(arg2, ...)`，而 `arg2` 只有**第一个** token，寄存器号在 `rest` 里
⇒ `rd 3c 0a` 永远命中不了，一律回用法提示。改成从 `rest` 取第二个 token（与 `ldo` 同法）。

MCLK 频率代价：P4 的 LEDC 时钟源只有 XTAL 40M / PLL_F80M 80M / RC_FAST ~20M，**没有 24M
整数倍** ⇒ 80MHz 小数分频到 ≈24.03MHz（+0.13%，XVCLK 允许 ±10%）。CSI 阶段若要求精确
24MHz，再评估用 `esp_clock_output`（`esp_hw_support/esp_clock_output.c`）。

## 5. 烧录后怎么判（烧录见 status §1：strap GPIO35→GND + RST1，烧完还要手动 RST1）

```powershell
python tools/serial_console.py --port COM23 --baud 115200 --seconds 25 --send-after 6 --send "test cam" --log _cam_test.log
```

判读：

| 回显 | 结论 |
|---|---|
| `PID: 0x300A=0x56 0x300B=0x40` | ✅ 电源 / FPC 接触 / 复位时序 / SCCB / MCLK 全通，可以进 C2（CSI 出图） |
| `SCCB 读 PID 失败`（NACK/超时） | 模块没应答。按序查：`+2V8`/`+1.5V` 实测电压 → CN1 插反/接触 → 万用表量 CN1.6/CN1.8 电平（探测时应为 3.3V / 0V） |
| `PID 不匹配` | SCCB 通了但 ID 不对：模块型号不符或地址不是 0x3C |
| 启动日志里 `camera probe failed` | 同上；`bringup_init()` 不会因它中断 |

> 顺带可验证 MCLK：`test cam` 期间量 `CN1.14`（或模组 pin20），应为 24MHz 方波（万用表频率档读得到）。
