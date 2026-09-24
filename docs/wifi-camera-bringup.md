# WiFi 推流 + 摄像头 — bring-up 方案 (V1 板)

> 2026-09-16 | 背景: 串口推流 (UART0@2M, 2.5fps) 已跑通。本篇是接下来两条链路的落地方案:
> **① WiFi 推流** (模组内 C6 + esp-hosted) ② **摄像头** (OV5640 MIPI-CSI)。
> 决策: 先做 WiFi (不依赖新硬件, 且能一次暴露两个真正的未知数), 再做摄像头。

---

## 一、WiFi: ESP32-P4 自己没有 WiFi, WiFi 在模组里的 C6

```
P4 (app)  ──esp_hosted host / SDIO──▶  ESP32-C6FH4 (slave 固件)  ──WiFi 6──▶  AP
   │ 标准 esp_wifi API (esp_wifi_remote 转发)          │
   └── 拿到 netif → socket/TCP 直接可用                 └── 天线 = 模组 pin2 (IPEX 座)
```

### 1.1 硬件事实 (核对模组手册 + 网表 + 实机日志)

| 项 | 结论 |
|---|---|
| P4↔C6 链路 | **模组内部 SDIO**, 占用 P4 的 **GPIO14-19** (模组 88 脚里唯独没引出这 6 个脚 = 内部消耗); 与 SD 卡 (GPIO39-44)、按键/I2S 不冲突。实机日志印证: `eh_sdio: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17` |
| C6 烧录脚 | 模组 pin4/5 = C6_U0RXD/U0TXD (**C6 烧录引脚**), pin9 = C6_IO9 (C6 strapping/BOOT)。**V1 是空网络, V2 直接删了网络** (见 §4.2) |
| 天线 | 模组 pin2 = LAN_OUT = **C6 的 ANT (IPEX 座)** → **必须外插 2.4G 天线**, 否则扫不到 AP |
| C6 固件 | C6 侧必须跑 **版本匹配的 esp-hosted slave 固件**; 实机验证结果: 鱼鹰模组**没有预烧** (见 §1.3) |

### 1.2 W0: WiFi 探测固件

代码: `firmware/main/peripherals/wifi_link.c/h` (esp_hosted 传输 + esp_wifi + 扫描 + 控制台 `wifi` 命令)
配置: `firmware/sdkconfig.wifi` (与主推流 sdkconfig 隔离) + `firmware/partitions.csv` (单 app 4MB)
构建: 双击 `firmware/_build_wifi.bat` (或见 §3 的环境变量) → `firmware/build_wifi/vbcpet.bin`

烧录 (独立固件, 随时可刷回主线推流版):
```bash
python tools/ai_flash.py --build-dir firmware/build_wifi --port COM8 --monitor 20
```

控制台 (115200, UART0/CH340): `wifi scan` / `wifi info` / `wifi conn <ssid> <password>` / `wifi status` / `wifi reconnect`

### 1.3 实机结果 (2026-09-16): C6 无应答 → 需要先给 C6 烧固件

```
I (1667) eh_sdio: transport[host]: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17
I (1678) eh_mcu_transport: transport_init: bus backend up      ← P4 侧 SDIO 主机 OK
W (1686) eh_sdio: Reset co-processor using GPIO[54]
E (3377) eh_host_port_sdio: sdio_card_fn_init failed           ← C6 连 SDIO 卡枚举都没应答
```

判据: 如果只是"固件版本不匹配", SDIO 卡枚举会先成功再报协议错; 现在是**卡枚举就失败** ⇒ C6 是空片(或没启动)。

**2026-09-17 补测: 两条独立通道都证明 "C6 没在跑"**

1. **SDIO 侧**: 上面这份日志用的就是 GPIO14-19 (`CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17`), 连**卡枚举**都过不去。
2. **UART 侧**: 上电、`C6_IO9` 悬空时量 `pin5 (C6_U0TXD)` = **0.12V**。
   判据: ESP32-C6 默认 `U0TXD = GPIO16`(IDF `soc/esp32c6/include/soc/uart_pins.h`), UART 的 TX **空闲恒为高**。
   所以只要 C6 有电、跑过 ROM, 无论它是停在 ROM 下载模式还是正常启动, pin5 都该稳在 ~3.3V。**0.12V = 那条线上没有任何东西在驱动。**
3. ⇒ 结论: **C6 要么没供电, 要么已经损坏**(损伤可能来自那次"三条线接错位"—— P4 的推挽输出与 C6 的 TX 对顶过, 足以打死 TX 焊盘)。
   注意其中 "TX 焊盘坏了" 这一种情况下, **esptool sync 也永远不可能成功**(同步应答要从 TX 回来), 所以 UART 这条路在 V1 上可能已经堵死。

低成本分类 (断电/上电各一次, 万用表):
- 断电量 `pin5 ↔ GND` 电阻 → ~0Ω ⇒ 只是飞线/焊锡短路, 清掉即可;
- 上电(先 `gp 32z` 释放 GPIO32 或**拆掉 pin9 那根线**), 量 **pin9**(C6 的 GPIO9 芯片内部有弱上拉)与 **pin10**(模组内部 10k 上拉) → 两个都 ≈0V ⇒ **C6 没有供电**, 是模组侧问题, V1 到此为止。
另外踩到一个坑: 当时探测固件在链路没通的情况下还去调 `esp_wifi_*`, 在 WiFi-RPC 的空队列上
`assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))` → panic → **重启循环** (屏幕一亮一灭)。
**已修**: `wifi_link_init()` 现在先 `esp_hosted_init()` + `esp_hosted_get_cp_info()` 验证链路,
不通就打印诊断并直接返回, **一行 WiFi API 都不碰**。

### 1.4 W1 → W2: C6 通了之后怎么推流 (推荐顺序)

1. **W1 联网**: `wifi conn <ssid> <pw>` → 等 `GOT_IP` (SSID/密码也可写成 Kconfig 默认值, 开机自动连)。
2. **W2 TCP 推流 (性价比最高)**: P4 侧加一个 TCP 客户端 transport, PC 侧 `pc-host/wifi_stream.py` 当 server,
   **协议不改** (仍是 `FRAME:<hex8>\n` + JPEG, demux v2 已是二进制自同步) → 显示管线一行不动。
   - 串口 2M ≈ 200KB/s 是现在 2.5fps 的瓶颈; WiFi (SDIO 40MHz 4-bit) 余量 10× 以上, 帧率有望 10fps+。
   - esp_hosted 3.x 的 host/CP 端口划分 (host 侧 49152-61439): 我们做 client 连 PC 不受影响。
3. **W3 WebRTC/H.264 (长期)**: 见 `docs/webrtc-bringup.md`, 风险最高, 放最后。

### 1.5 已知副作用: SD 卡与 hosted SDIO 共用 SDMMC 主机

esp_hosted 初始化后, 我们的 microSD 挂载失败:

```
I (3418) sdmmc_periph: sdmmc_host_init: SDMMC host already initialized, skipping init flow
E (3427) vfs_fat_sdmmc: sdmmc_card_init failed (0x105).
```

后续要么 SD 改 SPI 模式, 要么接受两者互斥 —— 记入待办 (V2 需决策)。

---

## 二、摄像头: OV5640 MIPI-CSI (模组已在手)

### 2.1 现状盘点

| 项 | 状态 |
|---|---|
| 硬件 | CN1 = 24pin 0.5mm FPC 座 ✅; +2V8 (LOD28/ME6211A28) + +1V5 (AMS1117-1.5) 已于 08-17 实测 2.79V/1.49V ✅ |
| 引脚 | MCLK=GPIO5, RSTB=GPIO12, PWDN=GPIO13, I2C=GPIO8/21 (共享), CSI 2-lane = 模组 pin41-46 |
| 固件 | **还没写** (只有 `board_config.h` 里的引脚宏) |
| 官方件 | IDF 自带 `esp_driver_cam` (CSI) / `esp_driver_isp` / `esp_driver_jpeg` (**P4 有硬件 JPEG 编解码**); 传感器驱动用官方 `espressif/esp_cam_sensor` 2.5.0 (targets 含 esp32p4, 内含 `sensors/ov5640/ov5640.c`) |
| 官方示例 | `examples/peripherals/camera/mipi_isp_dsi` (CSI→ISP→DSI, 与我们的板拓扑一致) + `common_components/sensor_init` |

**⚠️ 官方 OV5640 驱动的 MIPI 模式只有一种**:
`MIPI_2lane_24Minput_RGB565_LE_1280x720_14fps` (其余 4 种是 DVP)。即: 能直接拿 720p RGB565 @14fps,
再用 PPA 缩到 480×800 (16:9 → 3:5 竖屏需决定裁切/留边); 想要别的分辨率得自己补一份 MIPI 寄存器表。

### 2.2 分步计划

| 步 | 内容 | 风险/备注 |
|---|---|---|
| **C0** | 插上模组, 万用表复测 4 路电源 (DOVDD/AVDD/AF-VDD 2.8V, DVDD 1.5V), 确认 PWDN 极性 (expansion.md 记 "PWDNB 极性待确认") | 无板载稳压, 电源对了再动 I2C |
| **C1** | 传感器探测: PWDN/RST 时序 + SCCB(我们的共享 I2C 总线) + 自动探测打印型号/ID | 半天; 通了就证明 FPC/电源/I2C 全对 |
| **C2** | `esp_driver_cam` 起 CSI 2-lane, OV5640 出 RGB565 → PPA 缩放 → 刷 DSI 屏 (本地预览, 不依赖 WiFi) | 注意 **CSI lane 极性/顺序** (08-06 记过模组符号 D0/D1 两对 P/N 有反接争议) — 备"软件 lane swap" |
| **C3** | 接 **P4 硬件 JPEG 编码** → 摄像头画面 JPEG 上屏/上传 PC, 与现有显示+推流管线复用 | 之后才谈"桌宠长眼睛" (视觉/ESP-DL) |

分工: 官方给"传感器驱动/寄存器序列 + CSI/ISP/JPEG 驱动 + 示例胶水"; 我们写的是板级适配
(PWDN/RST 引脚、把 SCCB 挂到已有共享 I2C 总线而不是新建、CSI PHY 的 LDO 通道、接进现有 display_pipeline、lane 极性兜底)。

---

## 三、构建环境坑 (本次踩过并已固化)

| 坑 | 现象 | 解法 |
|---|---|---|
| `ESP_IDF_VERSION` 未设 | `esp_wifi_remote` 的 `Kconfig.idf_v$ESP_IDF_VERSION.in` 找不到 → `CONFIG_WIFI_RMT_*` 全缺失, 编译一堆 undeclared | 构建前 `set ESP_IDF_VERSION=5.5` (已写进 `firmware/_build_wifi.bat`) |
| 组件源连不上 | `Cannot establish a connection to the component registry` | 国内镜像: `IDF_COMPONENT_REGISTRY_URL=https://components.espressif.cn/` + `IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn/` |
| app 分区装不下 | `app partition is too small ... size 0x13b970` | esp_hosted 让 app 涨到 ~1.3MB → 统一改用 `firmware/partitions.csv` (单 app 4MB), 主线与 WiFi 版共用 |
| esp_hosted 的板级预设 | Kconfig 里的 `ESP_HOSTED_P4_C6_CORE_BOARD` 预设用 GPIO47-52 (**与我们的 MPU_INT/背光/LED 撞脚**) | **不要选它**; 保持 `ESP_HOSTED_P4_DEV_BOARD_NONE` + slot1 默认 **GPIO14-19** (匹配模组内部链路) |
| C6 固件构建 | `SDIO SW_AGGR: this ESP-IDF lacks the send-cap fix` / `#error "Not the current run"` (PACKET 是空桩) | 必须走 SW_AGGR + 给 IDF 打一行补丁 (§4.3), 工程见 `firmware_c6/` |

---

## 四、C6 固件更新通道 (2026-09-16 事故复盘)

### 4.1 三条路的能力对比

| 通道 | 原理 | 前提 | 本次可用 |
|---|---|---|---|
| **host 侧 slave OTA** | P4 经 SDIO 推镜像给 C6 (`esp_hosted_slave_ota()` + `_begin/_write/_end/_activate`), C6 侧 A/B 双 OTA 分区 | **C6 已在跑兼容的 slave 固件** | ❌ 先有鸡还是先有蛋 |
| **C6 UART 下载** | 模组 pin4/pin5 (C6_U0RXD/U0TXD) + pin9 (C6_IO9→GND) → ROM 下载 | 要飞线 (板子没引出) | ✅ 唯一可行 |
| **C6 复位 (GPIO54)** | 我们固件里 `CONFIG_ESP_HOSTED_HOST_RESET_GPIO=54` + `RESET_ACTIVE_LOW` + `CP_RESET_STRATEGY_ALWAYS` ⇒ 每次开机都会把 GPIO54 当 C6 复位脉冲拉低 1.5s 再放开 | **板上 GPIO54 (pin82) 是 NC**; 只有模组内部真的把 C6 EN 接到 GPIO54 才有用。实测该序列换不来任何 C6 应答(见 §1.3), 所以**目前按"GPIO54 与 C6 无关"处理** |
| **C6 USB** | C6 自带 USB-Serial-JTAG | C6 的 USB 脚是 GPIO12/13, 而模组只引出 C6_IO13/IO14, **凑不齐一对** | ❌ |

### 4.2 设计教训 (V2 复核必加项)

模组厂默认 "C6 出厂带 esp-hosted slave 固件" (P4C6 组合的卖点就是 C6 当 P4 的网卡), 但**没给空片/坏固件留恢复通道**;
我们自己画 V1/V2 时也把 **C6_U0RXD/U0TXD (pin4/5) + C6_IO9 (pin9) 留空** (V1 是三个单脚空网络, V2 直接删了网络),
当时的判断是 "C6 的串口用不到" —— **错在没把"恢复通道"当需求**。

| V2 增补 | 成本 | 省下的 |
|---|---|---|
| C6_U0RXD(pin4) / C6_U0TXD(pin5) / C6_IO9(pin9) + GND → 1×4 排针或 4 个测试焊盘, 丝印 `C6_DBG` | ≈0 | 一次刮邮票孔飞线 + 一天排查 |
| LAN_OUT (pin2) 保留 IPEX 焊盘 —— **有代价, 见下** | 低 | 换天线方案的灵活性 |

> ⚠️ **2026-09-17 修正 (查手册后)**: 手册 pin2 = "ESP32-C6FH4 的 ANT, IPEX1 代天线接口" ⇒ 它是**射频网络, 不是 GPIO**。
> 2.4GHz 上任何支节(stub)都会劣化驻波和效率, 所以**只有在确实需要第二路天线时才留焊盘, 而且必须按 50Ω 微带从 pin2 引出**; 否则宁可保持 NC。
> 留之前先量一次: 断电, 万用表通断档量 `pin2 ↔ 模组 IPEX 座中心针`。若导通 ⇒ pin2 就是 IPEX 的中心脚, 留焊盘等于直接并一根支节, 更不该留。

### 4.3 C6 slave 固件工程 (`firmware_c6/`)

官方 CP 工程 (`examples/wifi/sta/cp`) 的移植, 目标 esp32c6, 产物 `firmware_c6/build/vbcpet_c6_cp.bin`。

构建前提 (两处):
1. **IDF 补丁** (SW_AGGR 必需): `components/esp_driver_sdio/src/sdio_slave.c` 去掉 4092B 单次发送上限
   ```c
   - SDIO_SLAVE_CHECK(len > 0 && len <= 4092, "length out of range: (0, 4092]", ESP_ERR_INVALID_ARG);
   + SDIO_SLAVE_CHECK(len > 0, "len <= 0", ESP_ERR_INVALID_ARG);
   ```
   本机已打, 备份 `sdio_slave.c.bak-vbcpet-4092`。
2. 构建环境变量同 §3 (镜像 + `ESP_IDF_VERSION=5.5`)。

### 4.4 目标形态: 让 C6 也能无人值守更新

C6 带上固件后, 把官方 `examples/ota/coprocessor_ota/mcu_host` 接进 P4 固件:

```
P4 控制台/网络: cpota <url 或 串口分块>  →  esp_hosted_slave_ota_*()  →  C6 重启
```

这样 **P4 与 C6 两侧都能一条命令更新**, 板上一个烧录排针都不需要 —— 与 `tools/ai_flash.py`
合起来就是完整的 "AI 自动烧录" 闭环。(前提: C6 第一次得靠飞线带上固件, 之后永远走 OTA。)

---

## 五、V2 改板目标: **一根 USB 刷好 P4 和 C6** (无飞线/无排针/无测试点)

> 用户诉求: 之后复刻的人插一根 USB-C 就能把 P4 和 C6 的固件都刷好, 不需要任何手工接线。
> 结论: **可以做到**, 而且不需要多加任何 IC —— 让 **P4 当 C6 的编程器**。

### 5.1 为什么不能靠"两个 USB 设备挂一根线"

| 方案 | 可行性 |
|---|---|
| P4 USJ 与 C6 USB 并联到同一个 USB-C | ❌ 两个 USB 设备 PHY 不能并联 |
| 板载 USB hub (2 口) + 两颗芯片的 USB | ⚠️ 可行但要多一颗 hub IC; 且 **C6 的原生 USB 是 GPIO12(D-)/GPIO13(D+)**, 模组只引出 C6_IO13/IO14, **D- 没引出** → 目前不成立 (需在 V1 上核实; 若 C6 USB 实为 GPIO13/14 则此路可通) |
| 板载 CH340 接 C6 UART (+自动复位两管电路) | ⚠️ 可行, 但要多一颗桥接 IC + 又一根 USB 或 hub |

### 5.2 推荐方案: P4 作为 C6 的串口编程器 (零新增 IC)

硬件改动 (V2 原理图, 3 根 P4 线 + 1 个上拉):

| 网络 | 连接 | 说明 |
|---|---|---|
| `C6_U0RXD` (模组 pin4) | ← **P4 GPIO30** | P4 当 TX, 走 C6 的 UART0 下载 |
| `C6_U0TXD` (模组 pin5) | → **P4 GPIO31** | P4 当 RX |
| `C6_IO9` (模组 pin9) | ← **P4 GPIO32** + **10k 上拉到 3V3** | 上拉=正常启动; P4 拉低=下次复位进下载 |
| C6 复位 (EN) | **原理图上没有这个脚** | ⚠️ 模组手册 88 脚表里唯一的 CHIP_PU 是 **pin87 = P4 自己的 EN**; 模组引出的 C6 脚只有 pin2(ANT)/pin4/5(UART)/pin6-14(C6_IO4..15)。C6 的 EN **没有引出** ⇒ 若模组把它接到了某个 P4 GPIO, 那属于**模组内部连接** (跟 GPIO14-19 的内部 SDIO 一样), 板级原理图上看不到。**本方案不依赖它** (见 5.3 的降级路径) |
| C6 复位 (EN) 的**第二次结论** | **模组根本没有引出 C6 的复位脚** | 手册 88 脚表里唯一的 CHIP_PU 是 **pin87** (手册原文: "使能 **ESP32-P4** 芯片, 内部无 RC 复位, 必须外加"); pin82 只是普通 GPIO54。另外当前 esp_hosted 版本里也**没有** "给 P4 默认 CP 复位脚" 这种配置(grep 过 `managed_components/espressif__esp_hosted`, 无对应 Kconfig/default) ⇒ **"P4 软件复位 C6" 没有任何依据**, V2 按"整板断电"设计。详见 [`../hardware/p4c6-manual-crosscheck.md`](../hardware/p4c6-manual-crosscheck.md) §1/§6 |

选 GPIO30/31/32 的理由: 这三脚在 V2 上是空闲的 (毫米波弃用后释放), 且都在模组 P4 侧引脚 (pin57/58/59), 布线干净。
**不要** 占用 GPIO14-19 (模组内部 C6 SDIO) 和 GPIO24/25 (USJ)。

固件改动 (P4 侧):

1. 引入官方 **`espressif/esp-serial-flasher` v2.0.0** —— 它的用途就是"从一个 MCU 烧另一个 ESP 芯片",
   port 层我们只实现 3 件事: 发字节 / 收字节 / `loader_port_enter_bootloader()` (拉低 GPIO32 + 复位)。
2. 新命令 (控制台/网络均可):
   ```
   cpflash <url|分块>   # 收 C6 镜像 → 经 GPIO30/31 烧进 C6 (ROM 下载, 自动 sync)
   cpota   <url>        # 常规升级: esp_hosted_slave_ota() 经 SDIO 推给 C6 并重启 (无需 UART)
   ```
3. PC 侧 `tools/ai_flash.py` 增加 `--target c6`: 同一条 USB-C 先刷 P4, 再把 C6 镜像交给 P4 转发 → **一根线两颗芯片**。

### 5.3 关键前提与验证项 (打样前必做)

| # | 待确认 | 怎么确认 | 影响 |
|---|---|---|---|
| 1 | 空片 C6 上电后是否自动停在 ROM 下载模式 | 我们现在就要做: 飞线接上 C6 UART 后, **不拉低 IO9** 也试一次 esptool `--chip esp32c6 chip_id` | 若成立 ⇒ 复刻者拿到空片 C6 时 **什么都不用按**, P4 直接烧 |
| 2 | **模组内部有没有把 C6 EN 接到某个 P4 GPIO (怀疑 GPIO54/pin82)** —— ⚠️ **2026-09-17 手册已查: 表里没有这个脚, 本条降级为"存疑, 不作为设计依据"**, 下表只留作最后核实手段 | ① **直接问鱼鹰** (手册里"原理图 暂无", 只能问): "C6 的复位脚内部是否接到 P4 GPIO54?" ② 现有固件就有 `gp <pin>{h\|l\|r\|z}` 命令: `gp 54l` / `gp 54h` 驱动 GPIO54, **同时把 C6 的 UART pin5 接到 USB-TTL 的 RX** 看 C6 日志是否跟着出现/消失 (pin5 本来就要接) ③ 行为测试: 固定 GPIO54 电平 + 断电重上电, 看 host 日志里 C6 的 SDIO 是否枚举成功 (两种极性各一次) ④ 量 pin82 静态电平特征 | 决定"**坏固件** C6"能否被 P4 自行复位; 空片 C6 不依赖它 |
| 3 | C6 原生 USB 到底在哪两个脚 | 查 C6 数据手册 / 量模组 pin7/8 (C6_IO14/IO13) | 只有 GPIO13/14 才谈得上"hub 方案" |

### 5.3.1 不依赖 C6 EN 的降级路径 (方案照常成立)

| C6 当前状态 | 怎么烧 | 需要人做什么 |
|---|---|---|
| **空片** (复刻者拿新模组, 最常见) | ROM 上电即停在 UART 下载模式 → P4 直接烧 | **什么都不用做** |
| 有固件但**兼容** | 先 `cpota` 走 SDIO OTA (无需 UART) | 无 |
| 有固件但**不兼容/坏** | P4 拉低 `C6_IO9` → **拔插一次 USB** (整板断电重上电, C6 采样到 IO9=0 进下载) → P4 烧 | 拔插一次 USB (不是飞线/排针) |
| (若验证出 GPIO54 = C6 EN) | 上面这条也变成纯软件 | 无 |

配套: `C6_IO9` 加 **10k 上拉** (保证默认正常启动)。

> **2026-09-17 更新 (查完手册)**: 模组**没有**引出 C6 的 EN/CHIP_PU (88 脚表里唯一的是 pin87 = P4 自己的 EN), 也查不到 C6 EN 接到哪个 P4 GPIO。
> ⇒ **"整板断电重上电"是 V2 唯一能把 C6 弄进下载模式的手段**, 除非 C6 出厂就带着兼容固件、可以走 SDIO OTA。
> 所以 V2 除了那 3 根 GPIO 线, 还要**二选一**:
> - ① **让鱼鹰出厂预烧** `esp-hosted` slave 固件 → 之后永远走 OTA, **零新增硬件, 最省** (推荐先问这一条);
> - ② 给模组的 **ESP_3V3 加一个受 P4 控制的负载开关** → 把"整板断电"变成"P4 自己给模组断电"。

### 5.4 配套的两个小改动 (否则"一根线"不成立)

| 项 | 原因 |
|---|---|
| **VBUS_IN → +5V 单向 OR (SS34 或理想二极管)** | 现在没电池时 IP5306 VOUT≈1.9V, 插 USB 也点不亮整板 → 复刻者必须先装电池才能刷机 |
| **板上 BOOT/RST 两个小按键** (不是排针) | 万一根文件出问题, 有不拆机的兜底; 成本≈0, 且不影响"一根线"的正常流程 |

### 5.5 复刻者的最终体验 (目标)

```
1. 插上 USB-C (板子自己上电, 无需电池)
2. python tools/ai_flash.py --all      # 或双击 flash_all.bat
   → 刷 P4 (USJ 自动复位) → P4 起来后转发镜像 → P4 刷 C6 (UART ROM 下载)
   → 抓启动日志 → 验证 WiFi 连上 + 自检全绿
3. 全程零接线、零按键
```

