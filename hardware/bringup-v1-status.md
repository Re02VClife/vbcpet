# V1 板 bring-up 现状交接 — 2026-09-29

> **这一份是自包含的**：新会话读完它就能接手，不需要翻前面的对话。
> 历史过程见 [`../WORKLOG.md`](../WORKLOG.md)；V2 改板见 [`v2-execution-plan.md`](v2-execution-plan.md) / [`v2-netlist-checklist.md`](v2-netlist-checklist.md)。

---

## 0. 一分钟速览

| | |
|---|---|
| 板子 | **V1 实物板**（`JLC/ProPrj_桌面开发助手_2026-08-11.epro2`，本地、gitignore） |
| 主控 | 模组 `ESP32-P4C6-Core`（鱼鹰光电，88 pin castellation） |
| 固件 | `firmware/build_wifi/vbcpet.bin`（2.6 MB，`SDKCONFIG=sdkconfig.wifi`） |
| 串口 | **COM23** = 板载 CH340，115200 8N1（插拔后会变，认 `VID_1A86&PID_7523`） |
| 控制台接线 | 板子 `H1.2`(TXD, 模组 pin64/GPIO37) → 转接板 RX；`H1.1`(RXD, pin65/GPIO38) → TX；共地 |

**已结案 4 项**：LDO_VO4 供电 · I2C 总线 · SD 卡读 · 电源保护
**未结案 6 项**：LED 不亮 · MPU-6500 不应答 · 4-bit SD · **C6 本身没在运行**（无供电证据，EN 候选脚已全排除）· `0x28` 未知器件 · 摄像头（**判定模块故障，待换件**）
**下一步**：摄像头**换一个 OV5640 24pin 模块**（固件已就绪，换件后跑 `test cam` 即可）；其余见 §4

---

## 1. 环境与命令速查

```powershell
# 构建（务必带这 3 个环境变量，否则组件源连不上 / Kconfig 分支选错）
cd D:\vscode\vbcpet\firmware
$env:IDF_COMPONENT_REGISTRY_URL="https://components.espressif.cn/"
$env:IDF_COMPONENT_STORAGE_URL="https://components.espressif.cn/"
$env:ESP_IDF_VERSION="5.5"
& "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" _idf.py -B build_wifi -D SDKCONFIG=sdkconfig.wifi build
# 等价于双击 firmware\_build_wifi.bat
```

```powershell
# 烧录：先 strap 再烧（顺序不能反）
#   1) GPIO35 接 GND（按住 SW5）  2) 点一下 RST1  3) 松开 SW5   ← 实测只需 GPIO35，不必按 SW6
cd D:\vscode\vbcpet
python tools/ai_flash.py --build-dir firmware/build_wifi --port COM23 --before no_reset --monitor 20
# ⚠️ 烧完必须手动按 RST1 —— V1 没有 DTR/RTS 自动复位电路，esptool 的 hard reset 对它无效
```

```powershell
# 串口互动（最常用）
python tools/serial_console.py --port COM23 --baud 115200 --seconds 30 --send-after 4 --send "scan" --send "test imu"
# 逐脚电平扫描（会提示你输入万用表读数，最后生成报告）
python tools/gp_sweep.py --stage gp --port COM23 --out hardware/bringup-v1-measure2.md
```

**工具**：`tools/serial_console.py`（发命令/收日志）· `tools/ai_flash.py`（编译产物一键烧+验启动）· `tools/gp_sweep.py`（逐脚扫描）· `tools/enet_review.py`（网表复核）

---

## 2. 硬件现状

- **电源**：电池 + `+5V` / `3V3`。IP5306 移动电源芯片；`VBUS_IN → IP5306.VIN` 通。
  ⚠️ **H1 排针只有 TX/RX/GND，没有 5V** —— 板子靠电池供电，电池没电时整板不启动（串口 0 字节）。
- **I2C 总线**：`GPIO8=SDA` / `GPIO21=SCL`，外部 `R18`/`R9` 各 4.7k 上拉到 3V3。实测**上电空闲电压两条都是 3.3V**（正常）。
  挂在这条总线上的：触摸（FPC30）· MPU-6500（U7）· NFC（**未贴片**）· 摄像头（CN1，未接）。
- **microSD**：`GPIO39-44`（CLK/CMD/D0-D3）· `GPIO45=SD_CD`（R8 10k 上拉）· 卡座 `TF1`。
- **调试口**：V1 的 USB-C 走的是模组 `pin48/49`（native USB OTG），**不是** USJ —— 所以插 USB-C 刷不了，必须 CH340 + strap。
- **LED**：`GPIO51 (pin79)` → `R13 (33Ω)` → 4 颗 SK6812，藏在 `SW1~SW4`（凯华轴座 3528 反贴灯）里，**串联级联**。
- **摄像头**：`CN1`（24pin 0.5mm FPC 座）。详见 §5。

---

## 3. 已结案（4 项）

### 3.1 ✅ LDO_VO4 → GPIO39~48 组 IO 供电（2026-09-25 定案并已修进固件）

**现象**：GPIO39~48 整组推挽高电平只有 ~1.23V。覆盖 SD 卡 CLK/CMD/D0-3(39-44)、SD_CD(45)、PA_EN(46)、MPU_INT(47)、BL_PWM(48)。

**根因**：模组 `pin66 = ESP_LDO_VO4` 的**输出**，同时就是这组 pad 的 IO 供电轨。复位后停在 ~1.2V 的**默认档**，而固件从来没配置过它。

**证据**（三条独立互证）：
1. 手册 pin66 原文 `Output power supply (output voltage range 0.5-2.7V or 3.3V, maximum output current 0.2A)`
2. IDF `components/sdmmc/include/sd_pwr_ctrl_by_on_chip_ldo.h` 注释 *"set to `4` is the `LDO_VO4` is connected to power the SDMMC IO"*
3. 实测拆掉 pin66 外部连线后：`ldo 4 1800 / 2700 / 3300` 三档，**pin66 与 GPIO48 同步跟随 1.8 / 2.7 / 3.3V**

**修复**：`firmware/main/vbcpet_main.c` 的 `ldo_vo4_init()`，在 `app_main` **最早期**（早于 display/SD/audio/IMU）调用；
电压由 `Kconfig.projbuild` 的 `CONFIG_VBCPET_LDO_VO4_MV` 控制（默认 **3300**，设 0 = 不配置）。
handle 通过 `firmware/main/power_rail.h` 的 `g_ldo_vo4` 共享给控制台 `ldo` 命令（IDF 限制：adjustable 通道**不能被 acquire 两次**）。

⚠️ **注意 3300 走的是 "rail bypass"（直通输入轨），1800/2700 才是片内 LDO 调压档** —— 两档都能用。

**验证**：控制台 `ldo 4`（不带 mv）回 `已 acquire` = 启动期配置已生效；`test sd` 根目录能列文件。

### 3.2 ✅ I2C 总线（2026-09-25 定案，一个焊点都没动）

**现象**：`scan` 0x03~0x77 **117 个地址全部 FOUND 且 reg0=0x00**；`i2c_master_probe` 全部 timeout；`WHO_AM_I=0x00`。三个外设看起来"全坏"。

**根因**：`firmware/main/peripherals/i2c_bus.c` 里 `.trans_queue_depth = 8`。IDF `esp_driver_i2c/i2c_master.c:1119`：
```c
if (bus_config->trans_queue_depth) {          // ← 只要非 0 就进这里
    ESP_LOGW(TAG, "Please note i2c asynchronous ... user cannot get bus error from API.
                   ... And It's not compatible with ``i2c_master_probe``. ...");
    i2c_master->async_trans = true;
```
**async 模式下驱动不上报任何总线错误** ⇒ NACK / 超时 / 器件不存在 一律"成功"返回。

叠加第二个 bug：`i2c_bus.c:71` 的 `ESP_LOGI("... reg0=0x%02X", a, 0)` —— **打印的是硬编码常量 `0`**，真实 `val` 被丢弃。
两个假象互相印证，看起来就像"总线被恒久拉低"。

**修复**：`trans_queue_depth = 0`（同步模式）+ 修掉 `reg0` 打印。

**修复前后（同一块板、同一批器件）**：

| 项 | 修复前 | 修复后 |
|---|---|---|
| `scan` | 117 个全 FOUND | **`FOUND=2 NACK=115 TIMEOUT=0`** |
| 找到的设备 | （全假） | `0x28` (reg0=**0xFC**) + `0x55` |
| `test touch` | 未探测到 | **PASS** |
| `test imu` | `WHO_AM_I = 0x00`（假） | `WHO_AM_I read FAIL`（**真**） |

⇒ **I2C 总线一直是好的**。用户实测 SDA/SCL 上电都是 3.3V，与此完全吻合。

> ⚠️ **`0x55` 是触摸，不是 NFC** —— NFC 芯片（NT3H2111）**根本没贴片**，所以 0x55 上只有触摸。
> `touch.c` 的探测表 `{0x55, 0x24, 0x15, 0x38, 0x48, 0x5D}` 第一个就命中，`TOUCH_I2C_ADDR 0x24` **不用改**。

### 3.3 ✅ SD 卡

`test sd` 挂载成功，根目录列出 `ZIMAGE / ROOTFS~1.GZ / BOOT.SCR / MYD-Y6~1.DTB / MFG-IM~1 / SYSTEM~1`。

⚠️ 自检仍判 **FAIL** —— 因为这张卡是**米尔 i.MX6 的系统盘**（只读，`open write FAIL`），与硬件无关。换空卡即全绿。

### 3.4 ✅ 电源保护（V1 硬件已有，不必改）

`F2 = SMD0805-100-12`（1A PPTC）在 `+5V → MP2315.IN`；`D3 PTVS3V3S1UR` / `D4 PTVS5V0S1UR` 电源 TVS；`F1` 0.5A PPTC 在 USB-A。详见 [`v2-execution-plan.md`](v2-execution-plan.md) A1。

---

## 4. 未结案（6 项）

### 4.1 ❌ LED 不亮（SK6812 ×4）

**现状**：`test leds`（**裸 255 全亮**，绕过 `LED_DIM`）也不亮 ⇒ **不是亮度问题**。
（顺带记：`bringup.c:38` 的 `LED_DIM = 255*0.07 = 17`，平时状态灯只跑 6.7% 亮度，与本次无关但值得改。）

**链路**：`模组 pin79 (GPIO51)` → `R13 (33Ω)` → `LED1_DIN` → LED2 → LED3 → LED4，**串联级联**，4 颗都接 `+5V`/`GND`。
（那 4 颗灯就在 `SW1~SW4` 那四个"凯华轴座下灯位-3528反贴灯"里；网表的 `$1N114/$1N112/$1N110/$1N111` 是灯间的 DIN→DOUT。）

**软件侧已排除**：`sk6812.c` 最后改动远早于本轮；没有任何 RMT 冲突；`serial_display.c` 里那两行 `gpio_set_level(GPIO51,...)` 只在 `CONFIG_VBCPET_TRANSPORT_UART` 下跑，而 `sdkconfig.wifi` 里它是**关**的。

**待量**（从链头往链尾）——
1. 发 `gp 51h`，量**模组 pin79**：≈3.3V ⇒ 输出正常；≈0V/悬空 ⇒ 模组那个焊盘的问题
2. 断电量 **R13 两端通断**（33Ω）
3. 量 **LED1 的 +5V**
4. 目视 **SW1~SW4 那 4 颗 3528 反贴灯贴了没**

> ⚠️ 用户称"以前点亮过"，但未确认是否同一块板/同一时期。另注：SK6812 供 5V 而 GPIO51 只输出 3.3V，
> 5V 供电时 VIH 典型 3.5V —— 这是**边缘值**，但既然曾经亮过就先不算主要嫌疑。

### 4.2 ❌ MPU-6500 (0x68) 不应答 —— **唯一坐实的硬件嫌疑**

`scan` 结果里**没有 0x68**；`test imu` 报 `WHO_AM_I read FAIL`（同步模式下这是**真实**错误上报）。

⇒ 用户"可能是焊接没焊好"的判断只在这里成立。建议：先量 `U7.13 (VDD)` / `U7.8 (VDDIO)` 是不是 3.3V，再补焊（QFN-24 手工焊）。
顺带：`AD0` 决定地址是 `0x68` 还是 `0x69`，现在的 `scan` 已经可信，一扫就知道。

### 4.3 ❌ 4-bit SD

1-bit 正常（能挂载），4-bit 失败在 `sdmmc_init_sd_ssr`（切位宽后第一个读命令）。
⇒ D0/CLK/CMD 都好，嫌疑在 **DAT1~3（GPIO40/41/42）**。
**待量（断电蜂鸣档）**：`M1.68↔TF1.8`、`M1.69↔TF1.1`、`M1.70↔TF1.2`。

⚠️ 固件当前用 `sd_card.c:34` 的 `host.flags &= ~SDMMC_HOST_FLAG_4BIT;` **临时强制 1-bit**（诊断用），量完要决定去留。

### 4.4 ❌ C6 烧录通道 / C6 本身没在运行（2026-09-29 深挖）

**新增硬证据（2026-09-29）**：

| 检查 | 方法 | 结果 |
|---|---|---|
| `c6mon` 能不能用 | 被动监听 C6 的 UART | ❌ 以前**一直不能**：`uart: UART driver already installed`（`esp32_port_deinit` 漏删 driver）。**已修**，现在能跑 |
| C6 的 UART 到底说不说话 | `c6mon 12`（新固件，hex+ASCII） | **0 字节 / 12s**；`c6flash rom` 跑满 60s 也是 **0 字节** |
| 旧扫描里那句"C6 TX 有 128 字节" | `c6scan`（driver 只装一次、每态 flush 后监听 1.5s） | **18 个组合全 0 字节** ⇒ 旧数据是 **UART driver 反复重装的瞬态假象**，不是 C6 在说话 |
| C6 的 EN 在哪 | `c6scan` 扫 `GPIO54/53/52/0/49/50 × 高/低/高阻` | ❌ **没有一个是**（`ESP_HOSTED_HOST_RESET_GPIO=54` 的默认假设在本模组不成立） |
| C6 有没有供电 | 上电 `pin10 (C6_IO8)` = 0.27V；断电测 `pin10↔GND` / `pin10↔3V3` **均开路** | ⚠️ **手册所称"内部 10K 上拉"实物不存在** ⇒ 0.27V 只是浮空漏电、**无判据价值**；该脚与手册不符（手册第 2、3 处出入见 `p4c6-manual-crosscheck.md`） |
| SDIO 侧 | `wifi info` | `eh_sdio: Reset co-processor using GPIO[54]` → `sdio_card_fn_init failed` 每 240ms 重试（`sdmmc_io_rw_direct returned 0x107`=超时）→ 5s `bring-up timed out` |

⇒ **当前判断：模组里的 C6 根本没在运行**（不是"空片停在 ROM 下载模式"——那种情况 UART 会稳定打印 `waiting for download`）。

**修正旧结论**：09-16 的「SDIO 卡枚举失败 ⇒ C6 是空片」应读作「C6 没在运行」；
09-17 的「pin5 = 0.12V ⇒ C6 没在跑」方向对但判据弱（TX 脚会被外部漏电影响）。

**下一步 —— 按正确流程重试烧 C6**（2026-09-29 更新，之前每轮都漏了第 ② 步与 GPIO8）：

```
① pin9 (C6_IO9) → GND  且  pin10 (C6_IO8) → 3V3（串 10k）
   ← GPIO8 必须为高！实测手册所称"模组内部 10K 上拉"不存在（开路）
② 整板断电（拔电池 + USB）等 5s   ← C6 只在这一刻采样两个 strap
③ 重新上电 → P4 启动后跑 c6flash（C6 此刻停在 ROM 下载模式等 sync）
④ 烧完: 拆掉 pin9 的地线（pin10 的上拉可留）→ 再整板断电重上电 → C6 才会跑 slave 固件
⑤ wifi info 应能读到 CP 信息 → wifi scan / wifi conn → 才能真正传数据
```

🎯 **根因（2026-09-29 查证官方文档后确定）**：C6 进 UART 下载模式要**两个** strap 同时满足 ——
[esptool: GPIO8 must also be driven High … GPIO8 = 0 and GPIO9 = 0 is invalid and will
trigger unexpected behavior](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/advanced-topics/boot-mode-selection.html)。
我们此前只管了 `GPIO9=0`，而 `pin10 (C6_IO8)` 实测**没有上拉**（对 GND/3V3 均开路）
⇒ `GPIO8` 不是高 ⇒ **无效 strap 组合** ⇒ C6 不打印、不进下载、sync 永远无响应。
旁证：`pin9` 悬空 = 3.3V 正是 C6 **内部 45k 上拉**的表现 ⇒ 芯片本身是好的。

🔴 **关键认识（2026-09-29）**：**P4 的 `RST1` 只复位 P4，不复位 C6**。模组没引出 C6 的 EN，
而 C6 **只在复位那一刻采样 strap** ⇒ 只要 C6 一直跑着（哪怕 P4 重启一百次），
`c6flash` 的 sync 就永远没人回应 —— 这是此前所有 sync 失败的流程性原因。

🔴 **V2 必查项（优先级高于"C6 三根线"）**：C6 的**可控复位**从哪来 ——
本板"C6 进下载模式必须整板断电"是流程硬伤，V2 应给出 C6 EN 的可控路径（或出厂预烧 + OTA）。

**引脚事实**：模组 `pin4 (C6_U0RXD)` / `pin5 (C6_U0TXD)` / `pin9 (C6_IO9)` 在板上 **都是 NC**（V1 靠飞线：→ P4 `GPIO30`/`GPIO31`，pin9 直接接 GND，实测 0V）；C6 从没被成功烧过。模组**没有引出 C6 的 EN** ⇒ 进 ROM 下载模式只能整板断电。

**配置现状（2026-09-29 已改）**：`sdkconfig.wifi` 里 `C6_FLASH_ON_BOOT` / `WAKE_SCAN` 已**关掉**
（它们原来让每次开机白等 45 秒、并把串口刷屏、还长期占着 UART1 —— c6mon 跑不起来的帮凶）；
`C6_FLASH` 保留，控制台仍可手敲 `c6flash` / `c6mon [秒] [波特率]` / `c6scan`。

### 4.5 ❓ `0x28` 是谁？

修复后的 `scan` 找到两个器件：`0x28`（reg0=0xFC）和 `0x55`（触摸）。
网表里挂在 I2C 上的只有四个：触摸（FPC30）· MPU（U7，不在）· NFC（未贴片）· 摄像头（CN1，未接）。**`0x28` 不在这份名单里。**
屏是 TDDI 的（`touch.c` 注释提到 ST7123），`0x28` 有可能是屏侧的另一路。
想弄清楚：`rd 28 00` / `rd 28 A8` / `rd 28 FF` 读几个寄存器看着。

### 4.6 ⏳ 摄像头 OV5640 @ CN1（2026-09-28 首轮 → 09-29 复测，**未结案**）

> ⚠️ **09-29 撤回**：首轮那句"软件侧已 100% 排除"当时是**错的** —— 09-28 没发现
> `cam_power_sequence()` 结束时把 RESTB 一直摁在复位断言这个 bug，所以
> `test cam` / `cam pol` 两条证据**在当时是无效的**（它们测的是一个被摁在复位里的模块）。
> 详见 [`bringup-v1-camera.md`](bringup-v1-camera.md) §3.6。

**09-28 首轮观察**：
- I2C 总线正常（`0x28`/`0x55` 都读得到）· 复位/PWDN 两个脚实测都能被 `gp` 拉动
- `cam raw`（手动放开复位后**等 1s** 再发事务）→ `probe(0x3C)=NOT_FOUND(261)` ⇒ 不是"等待不够"
- `scan` 只找到 `0x28`+`0x55` ⇒ 摄像头在任何地址都不出现

**硬件侧**：`+2V8`/`+1V5` 实测 2.8V/1.5V 正常 · 座子↔模块 **24 针全通** · 模块轻微发热（已上电）
· 但 `CN1.3`/`CN1.5` 稳定在 3.3V（板上 4.7k 上拉电压，无人钳位）

**09-29 复测新增**：
- 用户**重新插拔**排线后复测，四条命令结果与 09-28 **逐字节相同** ⇒ 接触不良不是原因
- 手动走完整时序扫 **RSTB 极性 × PWDN 极性 4 组合** → **全部 NOT_FOUND**，
  且耗时**逐字相同**（141µs / 171µs / 169µs）⇒ 模块对这两个脚毫无反应
- 🐞 **找到并修复一个真 bug**：`cam_power_sequence()` 末尾把 RESTB 停在复位断言
  （把内核 `gpiod_set_value()` 的逻辑值当物理电平抄了；本板 `REST` 低有效 ⇒ 释放 = 物理高）
- ❓ **MCLK 从未实测**：代码里只有 `ledc_*_config()` 的返回值。而 OV5640
  **没有 XVCLK 就完全不应答 SCCB** —— 恰好吻合现象。已新增 **`cam mclk`**
  （PCNT 实测 GPIO5 脉冲频率），待烧录验证
- ✅ 已排除 **DVP/MIPI 模块混淆**（用户确认插的是 08-03 采购的 MIPI 版）
- ✅ **板上到 CN1 的每一条信号链路都验证通过**（慢速方波 + 万用表看读数是否跟随跳动）：

  | 信号 | CN1 脚 | 结果 |
  |---|---|---|
  | CAM_MCLK | 14 | 3.3V（`gp 5h`）+ LEDC 运行时 0.75V ⇒ **通且在翻转** |
  | CAM_RSTB | 6 | 跟随 0↔3.3V |
  | CAM_PWDN | 8 | 跟随 0↔3.3V |
  | I2C_SDA | 3 | 跟随 0↔3.3V |
  | I2C_SCL | 5 | 跟随 0↔3.3V |
  | +2V8 / +1V5 | 4,11,23 / 10 | 2.8V / 1.5V |

- 🐞 `cam mclk` 误报 0 个脉冲的原因已定位：`ledc_channel_config()` 设
  `GPIO_MODE_OUTPUT` 时会关掉输入缓冲，而 PCNT 走 GPIO matrix 的**输入**路径
  ⇒ **测量方法本身破坏了被测信号**（修法：补 `gpio_input_enable()`，待下次烧录验证）

- ✅ **拿到 24pin 模块规格书（仁恒影像）并逐脚比对：引脚定义与本板 CN1 完全一致**
  —— `SDA=3 / SCL=5 / REST=6 / PWDN=8 / MCLK=14 / DVDD=10 / DOVDD=11 / AVDD=4 /
  AF-VCC=23 / GND=2,15,24 / STROBE=1 / NC=7,9,17,19,21`；差分对
  `MDP1/MDN1=12/13`、`MCP/MCN=16/18`、`MDP0/MDN0=20/22`。电源与光学参数也一致
  （AVDD 2.8V / DVDD 1.5V / DOVDD 2.8V；500W、4P+IR、EFL 3.2、F/2.6）
  ⇒ **板子的设计是对的**

⇒ **当前状态：未结案，但「板子侧」与「pinout」都已排除。** 只剩两处：
**① CN1 座子触点 ↔ 排线金手指的接触**（首轮那句"24 针全通"测的是排线两端，不含座子）；
**② 模块本体故障**。详见 [`bringup-v1-camera.md`](bringup-v1-camera.md) §3.6.8–3.6.11。

---

## 5. 摄像头验证（下一步）

### 已知事实

| 项 | 连接 | 出处 |
|---|---|---|
| 模块 | **OV5640，24pin，无板载稳压** | [`final-pinmap.md`](final-pinmap.md):157 · [`expansion.md`](expansion.md) 三节 |
| 座子 | `CN1` = KH-FG0.5-H2.0-**24PIN**（FPC 0.5mm） | 网表 |
| 数据 | CSI **2-lane**：`M1.41-46 → CN1.22/20/16/18/13/12`（D0P/N, D1P/N, CLKP/N） | 网表 + final-pinmap:137 |
| 时钟 | `CAM_MCLK = GPIO5`（**CN1.14**，OV5640 XVCLK 需 24MHz） | `board_config.h:72` + 网表 |
| 复位 | `CAM_RSTB = GPIO12`（**CN1.6**，低有效） | `board_config.h:73` + 网表 |
| 掉电 | `CAM_PWDN = GPIO13`（**CN1.8**，高有效） | `board_config.h:74` + 网表 |
| **SCCB 地址** | **`0x3C`** —— 走**共享 I2C**（`CN1.3=SDA` / `CN1.5=SCL`） | final-pinmap:155 |
| 供电 | `+2V8` (ME6211A28) → DOVDD/AVDD/AF-VCC（CN1.4/11/23）；`+1.5V` (AMS1117-1.5) → DVDD（CN1.10） | final-pinmap:157 + 网表 |

> ⚠️ **2026-09-28 订正**：上面第三列的"pin20/27/28"是 **M1（模组）脚号**，CN1 侧是
> **14/6/8**。另：那份 `FYRK3399-OV5640-100FPC规格书` 是 **30pin** 版本（已弃用，
> **2026-09-28 已删除**），**不要**拿它当 24pin 模块的接线依据。
> 今天新查证的逐脚表 + 固件探测路径 + **实测结论**见 [`bringup-v1-camera.md`](bringup-v1-camera.md)。


### 关键前提（已具备）

✅ **I2C 总线已修好**（§3.2）—— 这是 SCCB 的物理通道，之前被 async 配置假象挡住，现在扫得准了。

⚠️ **CSI 是差分信号，不吃逻辑电平** —— 所以它从头到尾**不受** GPIO39~48 那个 LDO 案影响。

### 固件侧现状：**探测路径已就位（2026-09-28），CSI/ISP 还没写**

`peripherals/camera.c` 新增 `camera_probe()`：LEDC 在 GPIO5 出 24MHz MCLK → PWDN/RSTB 上电时序 →
SCCB(0x3C) 读 `0x300A/0x300B`（期望 `0x56/0x40`）；已挂进 `bringup_init()`，并有 `test cam` 自检。
细节与判读表见 [`bringup-v1-camera.md`](bringup-v1-camera.md)。

⚠️ **原先这里写的"第 1 步：直接 `scan` 期望出现 `0x3C`"是错的判据**（2026-09-28 实测订正）：
`CAM_RSTB`(CN1.6) / `CAM_PWDN`(CN1.8) 在板上**没有任何上下拉**（网表里这两条网只有 M1 和 CN1
两个脚），复位后浮空 ⇒ OV5640 停在未定义态；且 GPIO5 复位后不是时钟输出，没有 XVCLK 也不会应答；
再者 `scan` 用的是 8 位"读 reg0"直读法，而 OV5640 寄存器是 16 位地址。今天用 `gp 13l`/`gp 12h`
把两个控制脚拉到正确极性后再 `scan`，**依然没有 0x3C** —— 这属于预期结果，不是判据。

### 验证阶梯（2026-09-28 更新）

**第 1 步**：烧入今天的固件（含 `camera_probe` + `rd` 修复），然后
```powershell
python tools/serial_console.py --port COM23 --baud 115200 --seconds 25 --send-after 6 --send "test cam" --log _cam_test.log
```
**期望**：`PID: 0x300A=0x56 0x300B=0x40` ⇒ 电源 / FPC 接触 / 复位时序 / SCCB / MCLK 全通。
不出现 ⇒ 查 `+2V8` / `+1.5V` 实测电压、CN1 插反/接触、CN1.6/CN1.8 电平（探测时应 3.3V / 0V）。
（详见 [`bringup-v1-camera.md`](bringup-v1-camera.md) §5 的判读表。）

**第 2 步**：`rd 3c 0a` / `rd 3c 0b` 直读寄存器交叉验证（该命令的解析 bug 今天已修）。

**第 3 步（要写代码）**：拉 `espressif/esp_cam_sensor` 到 `firmware/main/idf_component.yml`，
写 CSI 初始化 + 最小抓帧（`test cam` 这个入口已被探测占用，抓帧另起名字）。
配置要点：2-lane、`CAM_MCLK` 24MHz、`CAM_RSTB`/`CAM_PWDN` 时序、CSI 走 `M1.41-46`。

**第 4 步**：出图后接到显示管线（`display_pipeline_*`）或推流。

### 踩坑提醒

- 改完固件烧录**必须 strap + 手动 RST1**（§1）
- **控制台复位后 1-3 分钟会僵死**（见 §6），所以抓启动日志要抢时间 —— 用后台监听，让用户按 RST，别等
- `sdkconfig.wifi` **含明文 WiFi 密码，已在 `.gitignore` 里，不要提交**

---

## 6. 已知坑（必读）

| # | 坑 | 应对 |
|---|---|---|
| 1 | **UART0 控制台复位后 1-3 分钟僵死**（bug B1）| 发命令没反应就按 `RST1`。抓启动日志要抢窗口 |
| 2 | `serial_console.py` **吃首字符**（曾把 `ldo 4 3300` 变成 `?ldo`，害得整轮 LDO 验证白跑）| 已修（连接后先发换行热身）。但仍偶发，发关键命令**看回显确认** |
| 3 | V1 **没有 DTR/RTS 自动复位** | 烧录必须 strap（GPIO35→GND + RST1）；**烧完还要手动按一次 RST1** |
| 4 | `dl` 命令置的 `FORCE_DOWNLOAD_BOOT` 在 RTC 域，**软复位清不掉** | 用 `dl` 后要**断开电池重上电**才能回正常模式 |
| 5 | `test all` 第一项是 `buttons`，**要人工按键**，会卡住 | 用单项 `test <name>` |
| 6 | `c6flash` 开机自动扫描**白等 45 秒**并刷屏 | 建议关 `CONFIG_VBCPET_C6_FLASH_ON_BOOT` / `VBCPET_C6_WAKE_SCAN` |
| 7 | 板子靠**电池供电**，H1 排针没有 5V | 串口 0 字节时先确认电池有电 / 状态灯 |
| 8 | `sdkconfig.wifi` 有**明文 WiFi 密码** | 已 gitignore，别提交 |
| 9 | 根目录那份 `FYRK3399-OV5640-100FPC规格书` 是 **30pin** 版（已弃用，**2026-09-28 已删除**），拿它对 **24pin** 的 CN1 会全错位 | 24pin 权威依据 = 网表 CN1 逐脚，见 [`bringup-v1-camera.md`](bringup-v1-camera.md) §2；CN1 侧时钟/复位/掉电 = **14/6/8** |

---

## 7. 文件索引

| 文件 | 内容 |
|---|---|
| [`../WORKLOG.md`](../WORKLOG.md) | 完整工作日志（最新在最上面），2026-09-25 条目含 LDO + I2C 两个结案 |
| [`bringup-v1-measure.md`](bringup-v1-measure.md) · [`bringup-v1-ldo.md`](bringup-v1-ldo.md) | 逐脚电平扫描 / LDO 调压的原始实测记录 |
| [`bringup-v1-camera.md`](bringup-v1-camera.md) | **OV5640@CN1 验证**（09-28 实测 + 网表逐脚表 + 固件探测路径 + **未结案实测结论 §3.5**） |
| [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) | 模组手册 ↔ 网表逐脚对照；§6 是"要问鱼鹰的问题" |
| [`v2-execution-plan.md`](v2-execution-plan.md) | **V2 改板执行方案**（A1~A6，含 LDO_VO4 定案） |
| [`v2-netlist-checklist.md`](v2-netlist-checklist.md) | V2 网表逐项改动清单（已落地 10 项 / 待改 8 项 / 分支未决） |
| [`final-pinmap.md`](final-pinmap.md) | 权威引脚分配表 |
| `firmware/main/board_config.h` | 固件侧引脚定义 |
| `firmware/main/bringup.c` | 控制台命令（`help` 看全表） |

### 控制台命令（`help` 也是这个）

`help` `list` `test <name|all>` `scan` `led <state>` `gp <pin>{h|l|r|z}` `ldo <1-4> <mv>` `lcd` `bl <pct>` `brightness <pct>` `wifi <sub>` `c6flash` `c6mon` `reboot` `dl` `rd <addr> <reg>`

> ⚠️ `gp` 只支持 `pin <= 51`；`test all` 会卡在 buttons。
