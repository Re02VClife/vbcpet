# 换机器 / 交接清单 — 2026-09-29

> 用途：把这个项目挪到另一台电脑继续开发时，**哪些能靠 `git clone` 带走、哪些必须手动搬、哪些搬不走**。
> 配套：接手板子 bring-up 先读 [`../hardware/bringup-v1-status.md`](../hardware/bringup-v1-status.md)（自包含现状交接）。

---

## 0. 一句话总结

| 能不能带走 | 内容 |
|---|---|
| ✅ `git clone` 即得 | 固件源码、PC host、Live2D、全部文档、正式工具（`tools/serial_console.py` / `ai_flash.py` / `gp_sweep.py` …）、`firmware_c6` 的 C6 固件工程源码 |
| 📦 **必须手动拷贝** | **`JLC/` 整个目录**（嘉立创 EDA 原理图/PCB 工程，`.gitignore` 里有 `JLC/` + `*.epro2`）——**这是硬件设计的唯一源文件** |
| 🔑 手动重建 | `firmware/sdkconfig.wifi`（含明文 WiFi 密码，gitignore）；基线已由 `firmware/sdkconfig.defaults.wifi` 固化 |
| 🩹 手动重打 | **ESP-IDF 补丁**：`components/esp_driver_sdio/src/sdio_slave.c` 的 4092B 单次发送上限（编 `firmware_c6` 必需，见 §3.3） |
| ❌ 搬不走 | **V1 实物板 + 飞线状态 + 万用表读数**（只能在板子旁边继续 bring-up） |

---

## 1. 仓库与远程

```powershell
git clone https://github.com/Re02VClife/vbcpet.git
cd vbcpet
git log --oneline -5      # 确认拉到 2026-09-29 之后的提交（含 C6 深挖与 GPIO8 strap 结论）
```

- 分支：`main`（单分支开发）
- 远程：`origin = https://github.com/Re02VClife/vbcpet.git`

## 2. 必须手动搬的东西

**`JLC/`（4.7MB，6 个文件）—— 硬件设计源，丢了就得从零重画：**

```
JLC/ProPrj_桌面开发助手_2026-08-11.epro2   ← V1 已打样版（含 D1/USB 线宽两处待验证疑点）
JLC/ProPrj_桌面开发助手_2026-08-17.epro2
JLC/ProPrj_桌面开发助手_2026-08-20.epro2
JLC/原理图_2026-09-15.enet                 ← V2 原理图网表
JLC/Netlist_PCB1_2026-09-15.enet           ← V2 PCB 网表（v2-netlist-checklist.md 的依据）
JLC/pcb-auto-router_v1.36.98.eext          ← EDA 插件
```

挪过去之后，`tools/schematic_snapshot.py` / `tools/enet_review.py` 才能继续做网表复核。

## 3. 环境搭建

### 3.1 ESP-IDF

- 版本：**v5.5.3**（`D:\Espressif\frameworks\esp-idf-v5.5.3`）
- Python env：`D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`
- ⚠️ 路径硬编码在两处，换机器要改：
  - [`firmware/_idf.py`](../firmware/_idf.py)（包装器：剥 `MSYSTEM` + 设工具链 env）
  - `firmware/_build_wifi.bat` / `firmware_c6/_build_c6.bat` 里的 `PY=`

### 3.2 组件源（国内镜像，必须）

```powershell
$env:IDF_COMPONENT_REGISTRY_URL="https://components.espressif.cn/"
$env:IDF_COMPONENT_STORAGE_URL="https://components.espressif.cn/"
$env:ESP_IDF_VERSION="5.5"     # 不设则 esp_wifi_remote 的 Kconfig 分支缺失, 编译必挂
```

`managed_components/` 不入库，首次构建会自动下载（esp_hosted / esp_wifi_remote / esp-serial-flasher / esp_codec_dev）。

### 3.3 IDF 补丁（只在编 C6 固件时需要）

`components/esp_driver_sdio/src/sdio_slave.c`：

```c
- SDIO_SLAVE_CHECK(len > 0 && len <= 4092, "length out of range: (0, 4092]", ESP_ERR_INVALID_ARG);
+ SDIO_SLAVE_CHECK(len > 0, "len <= 0", ESP_ERR_INVALID_ARG);
```

原因：esp-hosted 的 **SDIO SW_AGGR** 需要单次发送 > 4092B，否则编 `firmware_c6` 会报
`SDIO SW_AGGR: this ESP-IDF lacks the send-cap fix`。详见 [`wifi-camera-bringup.md`](wifi-camera-bringup.md) §4.3。

## 4. 构建 / 烧录

```powershell
cd firmware

# 主线 (UART0 推流, 480×800 DSI)
python _idf.py -B build -D SDKCONFIG=sdkconfig build

# WiFi 版 (C6 + esp-hosted) —— 新机器第一次会由 sdkconfig.defaults.wifi 生成基线
#   然后 menuconfig → "vbcpet WiFi" → 填 SSID/PASSWORD
python _idf.py -B build_wifi -D SDKCONFIG=sdkconfig.wifi -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wifi" build

# C6 协处理器固件 (需要 §3.3 的 IDF 补丁)
cd ..\firmware_c6 && _build_c6.bat
```

烧录：**V1 板必须 strap**（`GPIO35→GND` + 点 RST1），烧完再手动按一次 RST1：

```powershell
cd D:\vscode\vbcpet
python tools/ai_flash.py --build-dir firmware/build_wifi --port COMxx --before no_reset --kill-stale --monitor 20
```

串口是板载 **CH340**（`VID_1A86&PID_7523`，口号随插拔变化，用 `python tools/ai_flash.py --list-ports` 认）。

## 5. 搬不走的东西（只能在板子旁边做）

- V1 实物板的**飞线状态**：目前 `pin9 (C6_IO9) → GND`，`pin4 → GPIO30`，`pin5 → GPIO31`，**`pin10 (C6_IO8)` 待加 10k 上拉到 3V3**
- 万用表实测读数（pin9/pin10/电源轨等）——已尽量记进 `hardware/bringup-v1-status.md`
- 板上固件版本：`firmware/build_wifi/vbcpet.bin`（09-29 构建，含 `c6scan` 与 `c6mon` 修复）

## 6. 当前进度快照（2026-09-29）

| 模块 | 状态 |
|---|---|
| 显示 (DSI ST7102 480×800) | ✅ 点亮 + Live2D 推流（UART0 2M, ~2.5fps） |
| Live2D / PC host | ✅ 可用 |
| I2C 总线 / 触摸 / LDO_VO4 | ✅ 结案 |
| SD 卡 | ✅ 1-bit 可用（4-bit 待查 DAT1~3） |
| 摄像头 OV5640@CN1 | 🔧 探测固件就绪，模块判定故障待换件 |
| **C6 / WiFi** | 🔴 **根因已定位：`C6_IO8` 无上拉 ⇒ `GPIO8=0 + GPIO9=0` 是无效 strap**（见 `bringup-v1-status.md` §4.4）。下一步：pin10 加 10k 上拉到 3V3 → 整板断电重上电 → 先试 SDIO（`wifi info`），再决定是否需要 UART 烧 C6 |
| V2 打样 | ⏳ 网表 8 项待改（含 **`C6_IO8` 接 P4 GPIO + 10k 上拉** 这条新增项） |
