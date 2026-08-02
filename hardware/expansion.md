# 外设拓展方案 — v1.0

> 日期: 2026-08-03 | 主控: ESP32-P4C6-Core | 基于 Netlist 08-03 + [final-pinmap.md](final-pinmap.md)
> 新增: 摄像头 / 六轴 / 舵机云台 / SD 卡

## 一、总览

| 功能 | 器件 | 接口 | GPIO 数 | 电源 |
|------|------|------|---------|------|
| 摄像头 | OV5640 (MIPI 30pin) | MIPI-CSI 2-lane + I2C | 3 (MCLK/RST/PWDN) | 2.8V/1.8V (模块自带则 3V3) |
| 六轴 IMU | MPU-6500 (QFN-24) | I2C | 1 (INT) | 3V3 |
| 舵机云台 ×2 | SG90/MG90S | LEDC/MCPWM | 2 | 5V (VBUS) |
| SD 卡 | microSD 座 | SDIO 4-bit | 6 (+1 CD) | 3V3 |

## 二、GPIO 分配(新增)

| GPIO | 功能 | 复用/备注 |
|------|------|-----------|
| **GPIO5** | CAM_MCLK (OV5640 XVCLK, 24MHz) | 原 I2S_MCLK 孤立网络,改为摄像头时钟输出 |
| **GPIO12** | CAM_RSTB | 摄像头复位 (低有效) |
| **GPIO13** | CAM_PWDN | 摄像头掉电 (高=关) |
| **GPIO24** | SERVO_YAW | 云台水平 PWM (50Hz, 500~2500µs) |
| **GPIO25** | SERVO_PITCH | 云台俯仰 PWM |
| **GPIO39** | SD_DAT0 | SD1_CDATA0 |
| **GPIO40** | SD_DAT1 | SD1_CDATA1 |
| **GPIO41** | SD_DAT2 | SD1_CDATA2 |
| **GPIO42** | SD_DAT3 | SD1_CDATA3 |
| **GPIO43** | SD_CLK | SD1_CCLK |
| **GPIO44** | SD_CMD | SD1_CCMD |
| **GPIO45** | SD_CD | SD1_CDATA4, 卡检测 (可选) |
| **GPIO47** | MPU_INT | 六轴中断输出 |
| I2C (GPIO8/21) | — | 共享: NFC(0x55) + 触摸(~0x24) + OV5640(0x3C) + MPU-6500(0x68) |

> 占用的 39-45 均带 SD1/GMAC 复用,本设计不使用 SDIO 双卡/以太网,无冲突。
> 舵机 PWM 用 P4 的 LEDC(1 个)或 MCPWM(2 个)均可,50Hz 方波即可驱动。

## 三、摄像头 — OV5640 MIPI

> ✅ 2026-08-03 确认: 采购 **MIPI 版 OV5640 模块** (0.5mm 30pin FPC, 2-lane)。
> 手上 ESP32-S3-CAM 的 **DVP 24pin 模块不能接 P4 MIPI-CSI**, 弃用, 留作 S3 开发板使用。

### 接线

| P4 信号 | → OV5640 模块 |
|---------|---------------|
| CSI_D0P (pin42) / CSI_D0N (pin41) | MIPI data lane0 ± |
| CSI_D1P (pin46) / CSI_D1N (pin45) | MIPI data lane1 ± |
| CSI_CLKP (pin43) / CSI_CLKN (pin44) | MIPI clock ± |
| GPIO5 (CAM_MCLK) | XVCLK (24MHz) |
| GPIO8/21 (I2C) | SCL / SDA (SCCB) |
| GPIO12 (CAM_RSTB) | RESETB (低有效) |
| GPIO13 (CAM_PWDN) | PWDN (高=掉电) |
| 3V3 / GND | 模块电源 (详见下) |

- 6 根 MIPI 走线做 **100Ω 差分对、等长**,远离 5V/舵机/背光。
- OV5640 用 **2-lane 模式**,最高 1080p@30(或 720p@60)。

### 电源 ⚠️ 待核实

市售 OV5640 MIPI 30pin 模块分两种:

| 模块类型 | 供电 | 板上接法 |
|----------|------|----------|
| **自带稳压**(多数 ESP32-S3 配套模块) | 只供 3V3 | 3V3 直供即可 |
| **不带稳压** | 需 AVDD 2.8V + DOVDD 1.8V | 加 2 颗 LDO: 3V3→2.8V (ME6211A28), 3V3→1.8V (ME6211A18) |

> **待确认**: 你的 30pin 模块排线定义/是否自带稳压。**请提供模块规格书或排线 pinout**,确认后定电源方案。DVDD 1.2V 一般由模块内部 LDO 产生。

### 功耗

OV5640 2-lane 1080p 约 **150mA** (发热明显,机壳需散热孔), 3V3 侧新增 ~200mA。

## 四、六轴 IMU — MPU-6500

| MPU-6500 引脚 | 接法 |
|---------------|------|
| VDD | 3V3 (1.8~3.3V OK) |
| VLOGIC | 3V3 (或 VDD) |
| GND | GND |
| SCL / SDA | I2C 总线 (GPIO21/8) |
| AD0 | GND → 地址 **0x68** |
| CS | 3V3 (禁用 SPI, 强制 I2C) |
| INT | **GPIO47** (运动/唤醒中断) |
| REGOUT | 100nF → GND |
| 去耦 | VDD 旁 100nF + 1µF |

- QFN-24 (3×3mm), I2C 400kHz。
- 6 轴: 3 轴加速度 ±16g + 3 轴陀螺 ±2000°/s。
- 用于: 拿起/晃动/倾斜检测 → 触发宠物动画、情绪、云台随动。

## 五、舵机云台 ×2

| 舵机 | 信号 | 电源 |
|------|------|------|
| Yaw (水平) | GPIO24 PWM | 5V |
| Pitch (俯仰) | GPIO25 PWM | 5V |

- **PWM**: 50Hz, 脉宽 500~2500µs (对应 -90°~+90°)。
- **电源**: VBUS 5V 直供 → 舵机座端 **470µF 电解 + 100nF** 就近缓冲。
  - 2 个 SG90/MG90S 轻载 ~400mA, USB 5V/3A 足够。
  - 若换重型舵机(>1A/个)需独立 5V 电源,此处不预留。
- **连接器**: 6pin PH2.0 座 (每舵机 VCC/PWM/GND), 或 2×3pin。
- 舵机 GND 与主板共地,电源粗线短走线。

## 六、SD 卡 — SDIO 4-bit

| 信号 | GPIO |
|------|------|
| SD_DAT0~3 | GPIO39~42 |
| SD_CLK | GPIO43 (25MHz) |
| SD_CMD | GPIO44 |
| SD_CD (卡检测, 可选) | GPIO45 (低=有卡) |

- 座子: microSD 推入式, 3.3V 供电 + 100nF 去耦。
- 用途: 离线音频/表情缓存、日志、OTA 包。
- SDIO 走线短、等长, 远离 CSI/MIPI 高频线。
- 若需省 1 脚, 可去掉 SD_CD (热插拔用轮询)。

## 七、电源预算(3V3 侧)

| 新增负载 | 电流 |
|----------|------|
| OV5640 | ~200mA |
| MPU-6500 | ~3mA |
| microSD (读写) | ~50mA |
| **新增小计** | ~250mA |

- 3V3 原预算 ~1.2A + 0.25A ≈ **1.45A** < MP2315 2A ✅ (余量 27%)
- 5V 侧: 原 560mA + 舵机 400mA ≈ 1A < USB 5V/3A ✅
- 建议将 MP2315 去耦 C4/C5 保持 10µF×2, 摄像头瞬态大, 可在 CSI 座旁加 10µF。

## 八、新增元件清单

| 位号 | 元件 | 封装/型号 |
|------|------|-----------|
| U_CAM | OV5640 MIPI 30pin 模块 | 含镜头 |
| J_CAM | 30pin 0.5mm FPC 座 | FPC-30P 翻盖 |
| U_MPU | MPU-6500 | QFN-24 3×3 |
| U_SD | microSD 卡座 | 推入式 |
| J_SERVO | PH2.0 6pin 座 | 舵机×2 |
| (LDO1) | 3V3→2.8V LDO (仅无稳压模块) | ME6211A28 SOT-23-5 |
| (LDO2) | 3V3→1.8V LDO (仅无稳压模块) | ME6211A18 SOT-23-5 |
| C_SD | 100nF + 10µF | SD 供电去耦 |
| C_SERVO | 470µF 电解 + 100nF | 舵机电源 |
| C_MPU | 100nF + 1µF | MPU 去耦 |

## 九、布局要点

1. **CSI 差分**: 6 线 100Ω 差分等长 (±0.5mm), 包地, 远离背光 Boost (12.8V 开关噪声)。
2. **摄像头座**放板边, 镜头朝外; FPC 走线短。
3. **舵机电源**粗线 (≥1mm), 470µF 靠近舵机座, 地与主板单点汇合。
4. **MPU-6500** 靠近板几何中心 (振动测量一致), 避开大电流/振动源。
5. **SDIO** 走线短等长, 与 MIPI 分区域。
6. 若加 LDO (2.8V/1.8V), 靠近摄像头座, 输出就近 1µF+100nF。

## 十、固件注意

| 项 | 说明 |
|----|------|
| 摄像头 | ESP-IDF **esp_cam** 驱动, OV5640 MIPI 2-lane, CSI 接口 |
| CAM_MCLK | GPIO5 配置为 24MHz 时钟输出 (I2S MCLK / REF_CLK) |
| MPU-6500 | I2C 0x68, 轮询或 INT 中断读取 accel/gyro |
| 舵机 | LEDC 50Hz, 500~2500µs; 上电先复位到中位再使能 |
| SD 卡 | sdmmc 驱动 4-bit SDIO, 25MHz, FAT 文件系统 |

## 十一、待核实清单

- [x] **摄像头方向**: MIPI 版 OV5640 (30pin FPC 模块, 2-lane); S3-CAM DVP 24pin 模块弃用
- [ ] **MIPI 版模块电源**: 是否自带稳压 → 决定 3V3 直供 or 2.8V/1.8V 双 LDO (需模块 pinout)
- [ ] **MIPI 版模块 30pin 排线定义**: 确认 MIPI lane/时钟/I2C/控制脚顺序 (各厂模块不同)
- [ ] 舵机型号/尺寸 → 确认扭矩与电流 (决定 470µF 是否足够)
- [ ] microSD 卡座型号 (推入式) 与封装
- [ ] 触摸 I2C 实际地址 (与 MPU/OV5640 确认不冲突: 现规划 0x24/0x3C/0x68/0x55)
