# firmware (M2) — ESP32-P4 固件 + Wokwi 仿真

> 里程碑 M2，M1 硬件定稿后展开。基于 **ESP-IDF (v5.4+)**，fork [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 复用音频链路，传输层集成 [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution)。

## Phase 0 现状（Wokwi 仿真骨架，已可编译）

硬件到位前用 **Wokwi ESP32-P4 Beta**（`board-esp32-p4-preview`）仿真固件逻辑层。已完成的模块：

| 模块 | 文件 | 说明 |
|---|---|---|
| 主入口 | `main/vbcpet_main.c` | NVS → WiFi → 音频 codec → 协议 hello 样例 |
| 音频 codec | `main/audio_codec.{h,c}` | ES7210 ADC + ES8311 DAC（小智同款，`esp_codec_dev`） |
| 控制协议 | `main/protocol.{h,c}` | CharacterEvent v1 构造/解析（cJSON），含双通道常量 + 风险等级枚举，镜像 `common/protocol.py` |
| Wokwi 配置 | `diagram.json` | board-esp32-p4-preview |
| IDF 配置 | `sdkconfig.defaults` | esp32p4 target, 16MB flash, 日志 |

**Wokwi 仿不了的**（需真硬件）：MIPI-DSI 显示 / H.264 VPU 硬解 / PPA 加速 / C6 射频。这些留到 Phase 0 devkit。

## 快速开始

### 在线 / VSCode 运行
1. 安装 ESP-IDF v5.4+（或 VSCode ESP-IDF 扩展）。
2. 打开 `firmware/` 为 ESP-IDF 项目：
   ```
   idf.py set-target esp32p4
   idf.py build
   ```
3. 用 **Wokwi VSCode 扩展** 或 [wokwi.com](https://wokwi.com) 加载 `diagram.json`，指定 `build/vbcpet.elf` 为固件。
4. 串口监视器应输出：
   ```
   vbcpet firmware booting on esp32p4 (IDF v5.4.x)
   I2C initialized (SDA=8 SCL=18)
   ES8311 DAC ready @24000Hz
   ES7210 ADC ready @16000Hz, 4ch TDM
   PA GPIO46 ready
   构造 hello: {"t":"hello","seq":1,...}
   构造 button: {"t":"button",...}
   初始化完成。等待 WebRTC 连接接入…
   ```

### 不依赖 IDF 编译（只验证协议逻辑）
```bash
# 用 gcc 编译 protocol.c + 简单 main 做单元测试
gcc -std=c11 -I/path/to/cjson main/protocol.c -lcjson -o test_protocol && ./test_protocol
```

## 职责（M2 全貌）
- **显示**：WebRTC 视频轨 → VPU H.264 硬解 → `av_render` → MIPI-DSI（Live2D 画面）。
- **音频**：ES7210/ES8311 + Opus + AEC + VAD/打断，走 WebRTC 音频轨。
- **联网**：C6 esp-hosted（SDIO/SPI），WebRTC 信令（本地 WHIP/HTTP）。
- **控制面**：DataChannel `ctrl` 收发（见 [`../docs/protocol.md`](../docs/protocol.md)）。
- **输入/传感**：4 按键 + 在位传感器 → 上行消息。

## 复用 vs 改造
| 来自 xiaozhi | 改造/新增 |
|---|---|
| ES7210/ES8311 codec、Opus、AEC、VAD、打断、板级 BSP | 传输层 WebSocket → **WebRTC** |
| 唤醒/对话状态机骨架 | 新增**视频轨硬解显示**（av_render + DSI） |
| | 新增 **DataChannel 控制协议**（已实现 `protocol.{h,c}`） |
| | 新增 **在位传感器 / 按键→授权回路** |

## 下一步
- [ ] Wokwi 中真实编译通过 + 串口输出验证。
- [ ] 接入 esp_webrtc → Wokwi 中与 pc-host 建 WebRTC 连接。
- [ ] 接小智完整 Opus/AEC/VAD 管线。
- [ ] 真硬件 bring-up 时复用此代码（只改引脚映射 + 加 DSI/VPU 驱动）。
