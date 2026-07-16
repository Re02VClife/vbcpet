# 引脚分配 (Pin Map) — v0 草案

> P4 具体 GPIO 号需按 **ESP32-P4 数据手册/模组 pinout** + IOMUX 能力确认；下表为**规划意图**，非最终定稿。用模组时部分引脚已固定。小智引脚方案（ES7210/ES8311）可作参考起点。

## 总线概览
| 总线 | 用途 | 挂载器件 |
|---|---|---|
| I2C0 | 配置 | ES7210(0x40)、ES8311(0x18)、ToF 传感器、触摸、可选 IO 扩展 |
| I2S0 | 音频数据 | ES7210(RX/TDM)、ES8311(TX/STD) — 共用 MCLK/BCLK/WS |
| MIPI-DSI | 显示 | DSI 屏（2 data lane + clock lane，差分） |
| SDIO(或 SPI) | C6 联网 | ESP32-C6（esp-hosted slave） |
| GPIO | 输入/控制 | 4 按键、在位传感器、PA_EN、背光 PWM、LED、复位 |
| USB2.0 | 数据/烧录 | USB-C（OTG） |

## I2C0（共享）
| 信号 | P4 引脚 | 备注 |
|---|---|---|
| I2C0_SDA | GPIOxx ⚠️ | 上拉 4.7k |
| I2C0_SCL | GPIOxx ⚠️ | 上拉 4.7k |

设备地址：ES8311=`0x18`，ES7210=`0x40`，ToF(VL53)=`0x29`，触摸/IO扩展按选型。

## I2S0（音频，ES7210 + ES8311 共用时钟）
| 信号 | P4 引脚 | 去向 |
|---|---|---|
| I2S_MCLK | GPIOxx ⚠️ | ES7210 + ES8311 |
| I2S_BCLK | GPIOxx ⚠️ | ES7210 + ES8311 |
| I2S_WS/LRCK | GPIOxx ⚠️ | ES7210 + ES8311 |
| I2S_DOUT | GPIOxx ⚠️ | → ES8311（DAC 数据入） |
| I2S_DIN | GPIOxx ⚠️ | ← ES7210（ADC 数据出，TDM 4ch） |
| PA_EN | GPIOxx | 功放使能（低电平省电） |

采样率：输入 16kHz（ASR 优化）/ 输出 24kHz（TTS）。ES7210 配 TDM 4ch，CH1=AEC 回采参考。

## MIPI-DSI（显示）
| 信号 | 备注 |
|---|---|
| DSI_CLK± / DSI_D0± / DSI_D1± | 差分对，等长、100Ω 差分阻抗 |
| BL_PWM | 背光调光 |
| LCD_RST | 屏复位 |
| TP_INT / TP_RST | 触摸（可选，走 I2C0） |

## C6 联网（SDIO 推荐）
| 信号 | 备注 |
|---|---|
| SDIO_CLK/CMD/D0-3 | 高速，等长约束；或退化为 SPI(SCLK/MOSI/MISO/CS)+HANDSHAKE |
| C6_EN / C6_BOOT | 复位/烧录控制 |

## 输入 / 传感 / 指示
| 信号 | P4 引脚 | 备注 |
|---|---|---|
| BTN_YES / BTN_NO / BTN_YES_ALWAYS / BTN_PANEL | GPIOxx ×4 | 内部上拉 + 硬件去抖 RC 可选 |
| DOCK_PORTRAIT / DOCK_LANDSCAPE | GPIOxx ×2 | 在位传感器（微动开关 GPIO / ToF 走 I2C 时用 INT） |
| HALL（可选） | GPIOxx | MagSafe 机型增强 |
| STATUS_LED | GPIOxx | WS2812 单线 或 普通 GPIO |
| SD_* | SDMMC/SPI | TF 卡 |

## 待办 ⚠️
- 依 ESP32-P4 IOMUX/模组 pinout 把所有 `GPIOxx` 落实为具体管脚，并检查高速外设（DSI/SDIO/USB/PSRAM）的固定引脚约束。
- Phase 0 用现成开发板时，直接读取其 BSP 的引脚定义填充本表作为参考。
