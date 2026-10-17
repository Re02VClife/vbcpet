# 摄像头 OV5640 @ CN1 — 交接文案（2026-09-29）

> **用法**：新开一个对话，把「§0 摘要」+「§1 起全文」粘进去即可接手，**不需要之前的对话历史**。
> 深层证据链（每一步是怎么被排除的）见 [`bringup-v1-camera.md`](bringup-v1-camera.md) §3.6.x。

---

## 0. 一句话现状

**板子这一侧全部验证通过，模块的 24pin 引脚定义也与板子逐脚一致，但摄像头在 I2C 上
完全不 ACK。** 只剩两处嫌疑：**① CN1 座子触点与排线金手指的接触**、**② 模块本体故障**。

## 1. 任务

在物理 **vbcpet V1** 板上验证 **OV5640 摄像头**（接在 **CN1**，24pin 0.5mm FPC 座）。
固件：ESP-IDF **v5.5.3**，WiFi 分支（构建用 `firmware/_build_wifi.bat`，`SDKCONFIG=sdkconfig.wifi`）。

## 2. 现象

模块**通电**（轻微发热）、**电源正常**，但在 I2C 上完全没有存在感：

- `test cam` / `cam` → `probe(0x3C) = ESP_ERR_NOT_FOUND(261)`（从机 NACK）
- SCCB 读 PID 失败 → 读不到 `0x300A/0x300B`（OV5640 期望 `0x56` / `0x40`）
- `scan` → 只有 `0x28` 和 `0x55`，摄像头在任何地址都不出现
- 对 `REST`/`PWDN` 的任意极性组合都毫无反应（4 组合耗时逐字相同：141 / 171 / 169 µs）

## 3. 已验证通过 —— **不要重复查这些**

| 项 | 结论 | 证据 |
|---|---|---|
| I2C 总线本身 | ✅ 正常 | `0x28`(reg0=0xFC) / `0x55` 都能读；`scan` = `FOUND=2 NACK=115 TIMEOUT=0` |
| 供电 | ✅ 2.8V / 1.5V | `+2V8`(CN1.4/11/23) 与 `+1V5`(CN1.10) 万用表实测 |
| **MCLK**（CN1.14） | ✅ 通且在翻转 | `gp 5h` 量到 **3.3V**（走线通、电平域就是 3.3V）；LEDC 运行时量到 **0.75V**（24MHz 的平均值） |
| **REST**（CN1.6） | ✅ 通 | 2 秒周期翻转，万用表读数**跟随 0↔3.3V** |
| **PWDN**（CN1.8） | ✅ 通 | 同上 |
| **SDA**（CN1.3） | ✅ 通 | 同上 |
| **SCL**（CN1.5） | ✅ 通 | 同上 |
| **pinout** | ✅ 与板子逐脚一致 | 24pin 规格书（仁恒影像）vs 网表，见 §5 |
| 复位时序 bug | ✅ 已修 | 见 §8 坑 2 |
| DVP/MIPI 模块混淆 | ✅ 已排除 | 用户确认插的是 2026-08-03 采购的 **MIPI 版** |

## 4. 剩余嫌疑（只剩这两个）

| # | 嫌疑 | 怎么验 |
|---|---|---|
| 1 | **CN1 座子触点 ↔ 排线金手指的接触** | 首轮那句"24 针全通"测的是**排线两端**，不含座子。可：在排线插入状态下从模块侧量电平；换一根排线；重新压接/换座子 |
| 2 | **模块本体故障** | 换一块同款（仁恒 24pin）或任意匹配该 pinout 的模块，跑 `test cam` |

## 5. 引脚定义（权威，已核对）

板子 `CN1` 与 24pin 规格书
[`datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf`](datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf)
**逐脚一致**：

| PIN | NAME | PIN | NAME | PIN | NAME |
|---|---|---|---|---|---|
| 1 | STROBE | 9 | NC | 17 | NC |
| 2 | AGND | 10 | **DVDD +1V5** | 18 | MCN |
| 3 | **SDA** | 11 | **DOVDD +2V8** | 19 | NC |
| 4 | **AVDD +2V8** | 12 | MDP1 | 20 | MDP0 |
| 5 | **SCL** | 13 | MDN1 | 21 | NC |
| 6 | **REST** | 14 | **MCLK** | 22 | MDN0 |
| 7 | NC | 15 | **GND** | 23 | AF-VCC +2V8 |
| 8 | **PWDN** | 16 | MCP | 24 | AF-GND |

- 控制：`SDA=GPIO8` / `SCL=GPIO21`（共享 I2C）· `REST=GPIO12` · `PWDN=GPIO13` · `MCLK=GPIO5`
- 差分（P4 CSI）：`MDP1/MDN1=12/13`、`MCP/MCN=16/18`、`MDP0/MDN0=20/22`
- 模组侧脚号：`M1.20=CAM_MCLK`、`M1.27=REST`、`M1.28=PWDN`、`M1.41-46=CSI`
- **PWDN 高有效**（OV5640 数据手册）⇒ 固件 `s_pwdn_active_high=1`，工作电平 = 低
- **REST 低有效** ⇒ 释放 = 物理高

## 6. 环境 / 操作要点

- **串口**：`COM23`（板载 CH340，115200 8N1）
- **烧录**（V1 无 DTR/RTS 自动复位）：
  1. `GPIO35`（模组 `M1.62`）**飞线到 GND**
  2. 按 `RST1`
  3. 串口出现 `boot:0x207/0x307 (DOWNLOAD)` + `waiting for download` 才算进下载模式
  4. `esptool --before no_reset --after no_reset write_flash ...`
  5. **先拔掉 GPIO35 那根线，再按 RST1**（否则又进下载模式）
  - ⚠️ `SW5` 按钮实测**不能**拉低 GPIO35 ⇒ 只能飞线
  - 一键脚本：`tools/_cam_flash_v2.py`（等下载模式 → 烧 → 抓启动日志 → 自动跑 cam 命令）
- **控制台命令**：`gp <pin>{h|l|r|z}` · `rd <addr> <reg>` · `scan` · `ldo <ch> <mv>` ·
  `test <name>` · `cam [addr|pol|raw|mclk]`
- 固件镜像：`firmware/build_wifi/vbcpet.bin`（已含复位修复 + `cam mclk` + PCNT 输入修复，
  **但 PCNT 那处修复尚未烧录验证**）

## 7. 固件侧已就绪（正常情况下无需再改）

- `firmware/main/peripherals/camera.c` / `camera.h`：`camera_probe()`、`camera_self_test()`、
  `camera_cmd()`
- 命令：`cam`（重跑探测）· `cam pol`（PWDN 极性 × 地址）· `cam raw`（不碰引脚只 probe）·
  `cam mclk`（PCNT 实测 MCLK 频率）
- 注册：`bringup_init()` 里调 `camera_probe()`；`test cam` 在 `self_test.c`

## 8. 本轮踩过的坑（新对话别重蹈）

1. **`cam mclk`（PCNT）会误报 0 个脉冲**：`ledc_channel_config()` 把 pad 设成
   `GPIO_MODE_OUTPUT` 时会**关掉输入缓冲**，而 PCNT 走 GPIO matrix 的**输入**路径。
   → **本会话就被它骗过一次**（据此误判"MCLK 没输出"），是万用表量 CN1.14 = 0.75V 纠正的。
   修法：PCNT 配好之后补 `gpio_input_enable(PIN_CAM_MCLK)`（只开输入、不动输出）。
2. **复位时序**：Linux 内核 `gpiod_set_value()` 的 `1/0` 是"**断言 / 释放**"，
   **不是物理电平**；本板 `REST` 低有效 ⇒ 释放 = **物理高**。
   照抄逻辑值会让模块被一直摁在复位里（09-28 的 `test cam`/`cam pol` 结论因此作废）。
3. **仁恒这份规格书是矢量 PDF**：无文本层、无位图 ⇒ 必须渲染成 PNG 再读图
   （`tools/_render_renheng.py`、`tools/_crop_renheng.py`）；且 PDF 文件名含中文，
   要先复制到 ASCII 路径，否则 `pymupdf` 报 `FileNotFoundError`。
4. **PowerShell here-string / here-doc 传 Python 会破坏内容** ⇒ 一律写成 `.py` 文件再跑。

## 9. 文档索引

| 文档 | 内容 |
|---|---|
| [`bringup-v1-camera.md`](bringup-v1-camera.md) | 摄像头专项，**§3.6.x 是完整证据链** |
| [`bringup-v1-status.md`](bringup-v1-status.md) | 全板自包含现状交接（§4.6 = 本项） |
| [`datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf`](datasheets/OV5640-MIPI-24pin-Renheng-20240329.pdf) | 24pin 模块规格书（权威 pinout 依据） |
| [`../WORKLOG.md`](../WORKLOG.md) | 顶部 2026-09-29 条目 |

## 10. 建议的新对话开场句

> 接手 vbcpet 摄像头 bring-up。先读 `hardware/bringup-v1-camera-handoff.md`。
> 板上链路和 pinout 都已验证通过，摄像头仍完全不 ACK，只剩「CN1 座子接触」和
> 「模块本体」两处。我想先验证 CN1 座子的接触情况。
