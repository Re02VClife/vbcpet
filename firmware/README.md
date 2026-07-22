# firmware (M2) — ESP32-P4 固件 + Wokwi 仿真

> 里程碑 M2。基于 **ESP-IDF (v5.5+)**，fork [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 复用音频链路，
> 传输层集成 [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution)。

## 当前状态

**ESP32-P4 开发板已到** — Phase 0 硬件验证即将开始。固件代码为骨架状态，协议层完整，音频接口定义了但实现占位。
### 初稿
开发一个桌面的vibecoding桌宠，可以在外接设备（开发板）上对接电脑上claude code或小龙虾等AI代理。由llm根据交流环境、当前状态控制桌宠的live2D动作（参考neuro sama），可语音对话（参考小智），通过WIFI或USB连接电脑进行通讯。
详细功能：
屏幕显示二次元live2d形象，
可对话，可通过对话控制电脑（如claude code编程）并实时反馈当前的工作状态（如编程工作阻塞时直接发声提醒）对接电脑上的claude code时，由于claude的输出文本一般都很大，需要总结、过滤、压缩到适合对话的文本量再发给硬件端。对话支持打断、声纹识别
有面板界面，可以显示token用量等相关信息
有快捷键（暂定4个快捷键yes、no、yes Always、面板）
硬件方面可参考小智项目，但因为需要运行更复杂的live2d交互，可能需要更高级的开发板，高开销的流程可以在电脑上或服务端处理完再传给硬件
和手机联动：可以充当手机支架，当手机竖着或是横着放在对应位置时，设备自动与手机互联，并将桌宠形象显示在手机上

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 主入口 | `main/vbcpet_main.c` | NVS → WiFi → 音频 codec → 协议 hello 样例 |
| 音频 codec 接口 | `main/audio_codec.{h,c}` | ES7210 ADC + ES8311 DAC（小智同款，`esp_codec_dev`），实现占位 |
| 控制协议 | `main/protocol.{h,c}` | CharacterEvent v1 构造/解析（cJSON），含双通道常量 + 风险等级枚举，**API 对齐 Python 版** |
| Wokwi 配置 | `diagram.json` | board-esp32-p4-preview |
| IDF 配置 | `sdkconfig.defaults` | esp32p4 target, 16MB flash |

### 尚未接入

- [ ] WebRTC (`esp_webrtc` 组件，`idf_component.yml` 中注释)
- [ ] MIPI-DSI 显示驱动
- [ ] H.264 VPU 硬解 (`av_render`)
- [ ] Opus 编解码 + AEC + VAD/打断
- [ ] C6 esp-hosted 联网（P4 无原生 WiFi）
- [ ] DataChannel 收发循环
- [ ] 引脚映射（全部 GPIOxx 占位符）

### Wokwi 仿不了的
MIPI-DSI 显示 / H.264 VPU 硬解 / PPA 加速 / C6 射频 — 需真硬件。

## 快速开始

### 编译（需 ESP-IDF v5.5+）

```bash
# 在 ESP-IDF CMD 终端中（非 Git Bash, IDF 5.5+ 不支持 MSys）
cd firmware
idf.py set-target esp32p4
idf.py build
```

### Wokwi 仿真

用 **Wokwi VSCode 扩展**加载 `diagram.json`，指定 `build/vbcpet.elf` 为固件。

串口监视器预期输出：
```
vbcpet firmware booting on esp32p4 (IDF v5.x.x)
I2C initialized (SDA=8 SCL=18)
ES8311 DAC ready @24000Hz
ES7210 ADC ready @16000Hz, 4ch TDM
PA GPIO46 ready
构造 hello: {"t":"hello","seq":1,...}
构造 button: {"t":"button",...}
初始化完成。等待 WebRTC 连接接入…
```

## 职责（M2 全貌）

- **显示**：WebRTC 视频轨 → VPU H.264 硬解 → `av_render` → MIPI-DSI
- **音频**：ES7210/ES8311 + Opus + AEC + VAD/打断，走 WebRTC 音频轨
- **联网**：C6 esp-hosted（SDIO/SPI），WebRTC 信令（本地 WHIP/HTTP）
- **控制面**：DataChannel `ctrl` + `realtime` 收发（见 [`../docs/protocol.md`](../docs/protocol.md)）
- **输入/传感**：4 按键 + 在位传感器 → 上行消息

## 参考

- [ESP32-P4 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf)
- [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution)
- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
