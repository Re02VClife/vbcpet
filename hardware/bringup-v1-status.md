# V1 板 bring-up 现状交接 — 2026-09-25

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
**未结案 5 项**：LED 不亮 · MPU-6500 不应答 · 4-bit SD · C6 烧录通道 · `0x28` 未知器件
**下一步**：摄像头验证（见 §5）

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

## 4. 未结案（5 项）

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

### 4.4 ❌ C6 烧录通道

- 模组 `pin4 (C6_U0RXD)` / `pin5 (C6_U0TXD)` / `pin9 (C6_IO9)` 在板上 **都是 NC**；C6 从没被成功烧过。
- `pin5` 实测 0.12V（UART TX 空闲应为 ~3.3V）⇒ C6 可能根本没在跑（三种可能都没排除，见 [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) §3.5b）。
- 模组**没有引出 C6 的 EN** ⇒ 进 ROM 下载模式只能整板断电。

⚠️ **`sdkconfig.wifi` 里 `CONFIG_VBCPET_C6_FLASH_ON_BOOT=y` + `VBCPET_C6_WAKE_SCAN=y` 开着**，导致**每次开机白等 45 秒**并把串口刷屏：
```
W (12760) c6flash: [扫描] GPIO54=0 → sync 无响应 (err=2)
...
E (77584) c6flash: 等 46s 仍没等到 C6 (err=2)
```
**建议改成 `n`**（除非你正在专门调 C6）。

### 4.5 ❓ `0x28` 是谁？

修复后的 `scan` 找到两个器件：`0x28`（reg0=0xFC）和 `0x55`（触摸）。
网表里挂在 I2C 上的只有四个：触摸（FPC30）· MPU（U7，不在）· NFC（未贴片）· 摄像头（CN1，未接）。**`0x28` 不在这份名单里。**
屏是 TDDI 的（`touch.c` 注释提到 ST7123），`0x28` 有可能是屏侧的另一路。
想弄清楚：`rd 28 00` / `rd 28 A8` / `rd 28 FF` 读几个寄存器看着。

---

## 5. 摄像头验证（下一步）

### 已知事实

| 项 | 连接 | 出处 |
|---|---|---|
| 模块 | **OV5640，24pin，无板载稳压** | [`final-pinmap.md`](final-pinmap.md):157 · [`expansion.md`](expansion.md) 三节 |
| 座子 | `CN1` = KH-FG0.5-H2.0-**24PIN**（FPC 0.5mm） | 网表 |
| 数据 | CSI **2-lane**：`M1.41-46 → CN1.22/20/16/18/13/12`（D0P/N, D1P/N, CLKP/N） | 网表 + final-pinmap:137 |
| 时钟 | `CAM_MCLK = GPIO5`（pin20，OV5640 XVCLK 需 24MHz） | `board_config.h:72` |
| 复位 | `CAM_RSTB = GPIO12`（pin27） | `board_config.h:73` |
| 掉电 | `CAM_PWDN = GPIO13`（pin28） | `board_config.h:74` |
| **SCCB 地址** | **`0x3C`** —— 走**共享 I2C**（`CN1.3=SDA` / `CN1.5=SCL`） | final-pinmap:155 |
| 供电 | `+2V8` (ME6211A28) → DOVDD/AVDD/AF-VCC；`+1.5V` (AMS1117-1.5) → DVDD | final-pinmap:157 |

### 关键前提（已具备）

✅ **I2C 总线已修好**（§3.2）—— 这是 SCCB 的物理通道，之前被 async 配置假象挡住，现在扫得准了。

⚠️ **CSI 是差分信号，不吃逻辑电平** —— 所以它从头到尾**不受** GPIO39~48 那个 LDO 案影响。

### 固件侧现状：**没有摄像头驱动**

`board_config.h:71` 只有三个引脚定义，注释写着"esp_cam; **本阶段暂不启用**"。全工程搜 `csi|camera|esp_video|ov5640` 只有这 4 行。

### 建议的验证阶梯（从零成本到高成本）

**第 1 步（不需要写驱动，5 分钟）**：插上 OV5640 模块到 `CN1`，然后
```powershell
python tools/serial_console.py --port COM23 --baud 115200 --seconds 20 --send-after 3 --send "scan"
```
**期望**：scan 结果里出现 **`0x3C`**。出现 ⇒ SCCB 通了、模块上电了、I2C 地址对上了。
不出现 ⇒ 先查 `+2V8` / `+1.5V` 两路供电（模块自己不带稳压，全靠板上那两颗 LDO）和 `CN1` 的插反/接触。

**第 2 步**：用 `rd` 直读 OV5640 的已知寄存器验证 SCCB 读写，例如
`rd 3C 0A`（PID 高字节，OV5640 期望 `0x56`）、`rd 3C 0B`（期望 `0x40`）。
这比 scan 更有说服力 —— 它证明**能读到正确的芯片 ID**。

**第 3 步（要写代码）**：拉 `espressif/esp_video`（或 `esp_cam_sensor`）到 `firmware/main/idf_component.yml`，
写 CSI 初始化 + SCCB 读写 + 一个最小抓帧自检（建议命名 `test cam`，挂在 `peripherals/self_test.c` 的 `ENTRIES[]` 里）。
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

---

## 7. 文件索引

| 文件 | 内容 |
|---|---|
| [`../WORKLOG.md`](../WORKLOG.md) | 完整工作日志（最新在最上面），2026-09-25 条目含 LDO + I2C 两个结案 |
| [`bringup-v1-measure.md`](bringup-v1-measure.md) · [`bringup-v1-ldo.md`](bringup-v1-ldo.md) | 逐脚电平扫描 / LDO 调压的原始实测记录 |
| [`p4c6-manual-crosscheck.md`](p4c6-manual-crosscheck.md) | 模组手册 ↔ 网表逐脚对照；§6 是"要问鱼鹰的问题" |
| [`v2-execution-plan.md`](v2-execution-plan.md) | **V2 改板执行方案**（A1~A6，含 LDO_VO4 定案） |
| [`v2-netlist-checklist.md`](v2-netlist-checklist.md) | V2 网表逐项改动清单（已落地 10 项 / 待改 8 项 / 分支未决） |
| [`final-pinmap.md`](final-pinmap.md) | 权威引脚分配表 |
| `firmware/main/board_config.h` | 固件侧引脚定义 |
| `firmware/main/bringup.c` | 控制台命令（`help` 看全表） |

### 控制台命令（`help` 也是这个）

`help` `list` `test <name|all>` `scan` `led <state>` `gp <pin>{h|l|r|z}` `ldo <1-4> <mv>` `lcd` `bl <pct>` `brightness <pct>` `wifi <sub>` `c6flash` `c6mon` `reboot` `dl` `rd <addr> <reg>`

> ⚠️ `gp` 只支持 `pin <= 51`；`test all` 会卡在 buttons。
