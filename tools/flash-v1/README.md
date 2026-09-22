# tools/flash-v1 — V1 板兜底烧录脚本

> 归档于 2026-09-17。这三个脚本原本散落在仓库根目录，带 `_` 前缀。
> V2 板的正常烧录走 [`../ai_flash.py`](../ai_flash.py)（按 VID:PID 认口、一条命令返回 JSON），
> **本目录只在 V1 板需要人工 strap 时使用**。

## 为什么还要留着

WORKLOG 2026-09-09 记录：V1 板控制台会在复位后 1–3 分钟内僵死，`dl` 命令失效，
**GPIO35→GND + GPIO36→3V3 + RST1 手动 strap 是唯一可靠的下载入口**。
V1 板要服役到 V2 到货，所以这套流程保留。

| 文件 | 作用 |
|---|---|
| `build_osptek.bat` | 设置 IDF 环境变量后 `idf.py -B build build`（osptek ST7102 驱动版本） |
| `flash_osptek.ps1` | 单个 COM 口上循环重试 `esptool write_flash`，最多 200 次 / 12 分钟，要求 ≥3 个 `Hash of data verified` |
| `sync_flash3.ps1` | 监听串口 360 秒：识别到 ROM download 模式（strap 成功）或 app 已启动（走 `dl`），再自动烧录 |

日志写在脚本同目录（`flash_osptek_log.txt` / `sync_flash3_log.txt`），不再污染仓库根目录。

## 用法

```powershell
# 1. 编译
.\tools\flash-v1\build_osptek.bat

# 2. 夹 strap (GPIO35→GND, GPIO36→3V3) 后监听并烧录
.\tools\flash-v1\sync_flash3.ps1

# 或者已知端口时直接重试烧录
.\tools\flash-v1\flash_osptek.ps1 -Port COM22
```

## 注意

- 脚本里的 ESP-IDF 路径 (`D:\Espressif`)、固件路径硬编码为本机布局，换机器要改。
- 烧录前先用 `ai_flash.py --kill-stale`（或手动）清掉占串口的残留进程，否则 esptool 会报 `port busy`。
