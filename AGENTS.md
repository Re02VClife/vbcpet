# vbcpet — AI 接手入口

> 只读这一页就能开工。细节都在大文档里，**按下面的"文档地图"按需读，不要整本通读**（全读约 10 万 token，且大半是过时的过程记录）。

## 项目一句话

AI 桌宠硬件：PC 端渲染 Live2D → 串口/WiFi 推流 → `ESP32-P4C6-Core` 模组（P4 主控 + 模组内 C6 当 WiFi 网卡）→ MIPI-DSI 480×800 屏，外挂触摸/按键/音频/摄像头/舵机/SD/NFC。

## 当前状态（2026-09-29，以 `hardware/bringup-v1-status.md` 为准）

| 模块 | 状态 |
|---|---|
| 显示 MIPI-DSI ST7102 480×800 | ✅ 点亮；Live2D 推流 UART0@2M ≈2.5fps |
| Live2D 渲染 / PC host | ✅ 可用 |
| I2C 总线 / 触摸 / LDO_VO4 | ✅ 结案（坑见下表） |
| SD 卡 | ✅ 1-bit 可用；4-bit 待查 DAT1~3 |
| 摄像头 OV5640 @ CN1 | 🔧 探测固件已就绪；**模块判定故障，待换件** |
| **C6 / WiFi** | 🔴 **根因已定位**：`C6_IO8` 无上拉 ⇒ `GPIO8=0`+`GPIO9=0` 是无效 strap ⇒ C6 不启动。下一步见文末 |
| V2 打样 | ⏳ 网表 8 项待改 |

## 🚨 必知坑（不知道会白干一天）

**烧录（V1 实物板）**
- 必须 strap：`GPIO35→GND` + 点 RST1，看到 `boot:0x207/0x307 (DOWNLOAD)` 才算进了下载模式
- 烧完**还要手动按一次 RST1**（板上无 DTR/RTS 自动复位）
- UART0 控制台复位后 **1~3 分钟会僵死**；抓启动日志要抢时间，没反应就按 RST1

**C6 / WiFi（这些是已排除的假设，别重复试）**
- ❌ `GPIO54/53/52/0/49/50` 都不是 C6 的 EN —— ⚠️ 但 `c6scan` 那次是在 `IO8` 未修好时跑的，**结论不可靠，修好 IO8 后值得重测**
- ❌ 旧日志里的"`C6 TX 有 128 字节`"是 **UART driver 反复重装的瞬态假象**，不是 C6 在说话
- ❌ 手册称 `pin10 (C6_IO8)` 有"模组内部 10K 上拉"——**实测对 GND/3V3 都是开路，不存在**
- ✅ `pin9 (C6_IO9)` 悬空 = 3.3V（C6 内部 45k 上拉）⇒ **芯片在、有电**
- ✅ C6 进下载模式要**两个 strap**：`GPIO9=0` **且** `GPIO8=1`
- ✅ **P4 的 RST1 不复位 C6**；模组没引出 C6 EN ⇒ 进下载模式只能**整板断电**（不是按 RST1）
- ✅ 烧录通道：**P4 当编程器**（`c6flash`），UART1 `GPIO30→模组pin4`、`GPIO31←模组pin5`，`pin9→GND`

**其它**
- I2C 驱动 `trans_queue_depth` 必须为 **0**（非 0 会切 async 模式，所有 NACK 都被报成"成功"）
- `GPIO39~48` 的 IO 供电由片内 `LDO_VO4` 决定，固件必须在 `app_main` 最早期配 3300mV
- V1 板靠**电池**供电，H1 排针没有 5V（串口 0 字节时先查电池）
- `sdkconfig.wifi` 含明文 WiFi 密码，已 gitignore；重建基线见 `firmware/sdkconfig.defaults.wifi`
- **git 走代理要单独配**：Clash 的"系统代理"对 git 无效
  `git config --global http.https://github.com.proxy http://127.0.0.1:7897`

## 文档地图（按需读，已标体量）

| 想知道 | 读 | 体量 | 建议读法 |
|---|---|---|---|
| 板子现状 / 每个未结案项的判据 | `hardware/bringup-v1-status.md` | 275 行 | 只读 §4（未结案）+ §6（坑） |
| 引脚 / 电源 / BOM | `hardware/final-pinmap.md`、`hardware/bom.md` | — | grep 具体信号名 |
| V2 改板清单 | `hardware/v2-netlist-checklist.md` | 短 | 全读 |
| 摄像头实测与结论 | `hardware/bringup-v1-camera.md` | 295 行 | 只读 §3.6、§5 |
| C6/WiFi 方案与踩坑 | `docs/wifi-camera-bringup.md` | 170 行 | 只读 §1.3、§4 |
| 模组手册 ↔ 网表逐脚对照 | `hardware/p4c6-manual-crosscheck.md` | 212 行 | grep 具体 pin |
| 推流协议 | `docs/protocol.md` | 5 KB | 全读 |
| 换机器要带什么 | `docs/handoff-checklist.md` | 85 行 | 全读 |
| **全过程历史** | `WORKLOG.md` | **1115 行 / 94 KB** | ⚠️ **只读前 150 行**（倒序＝最近 3 天）；查旧事用 grep |

## 阅读纪律（省 token）

1. **先 grep 定位，再 read 片段**（`offset`/`limit`），不要整文件读
2. `WORKLOG.md` **永远不要整读**：它倒序排列，`offset=1 limit=150` 就是最近 3 天
3. 不要读：`firmware/build*/**`、`firmware_c6/build/**`、`managed_components/**`、`live2d/node_modules/**`、`.venv/**`
4. 查"这个现象以前处理过没有"：`grep -n "关键词" WORKLOG.md` 就够
5. 改代码前先读目标文件，但**只读相关函数区间**

## 环境与常用命令

```powershell
cd firmware
$env:IDF_COMPONENT_REGISTRY_URL="https://components.espressif.cn/"
$env:IDF_COMPONENT_STORAGE_URL="https://components.espressif.cn/"
$env:ESP_IDF_VERSION="5.5"
# WiFi 版（C6 + esp-hosted）
python _idf.py -B build_wifi -D SDKCONFIG=sdkconfig.wifi -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wifi" build
# 主线（UART 推流）
python _idf.py -B build -D SDKCONFIG=sdkconfig build
```

```powershell
# 烧录（V1 先 strap，见"必知坑"）
python tools/ai_flash.py --build-dir firmware/build_wifi --port COMxx --before no_reset --kill-stale --monitor 20
# 串口发命令
python tools/serial_console.py --port COMxx --seconds 25 --send-after 3 --send "test cam"
```

控制台命令：`help` `test <name|all>` `scan` `gp <pin>{h|l|r|z}` `ldo <1-4> <mv>` `lcd` `bl <pct>`
`wifi <scan|info|conn|status>` `c6flash [rom]` `c6mon [秒] [波特率]` `c6scan` `cam [addr|pol|raw|mclk]` `rd <addr> <reg>` `reboot` `dl`

## 下一步（按优先级）

1. **C6**：`pin10 (C6_IO8)` 加 **10k 上拉到 3V3** → **整板断电重上电**（保持 `pin9=GND`）→
   先跑 `wifi info` 试 **SDIO**（若 C6 出厂已有 slave 固件，则零烧录即通）→ 不通再 `c6flash`
2. 摄像头：换 OV5640 24pin 模块 → `test cam`（固件已就绪，见 `hardware/bringup-v1-camera.md`）
3. MPU-6500 `WHO_AM_I=0x00`：唯一坐实的焊接嫌疑，量 U7 供电后补焊
4. V2：8 项网表改动，其中**新增**：`C6_IO8` 接 P4 GPIO + 10k 上拉（见 `hardware/v2-netlist-checklist.md`）
