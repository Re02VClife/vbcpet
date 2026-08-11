# 外设拓展方案 — v1.3

> ✅ **已实装 (2026-08-11)**: 本方案已全部画入原理图、网表复核通过并随板打样。GPIO28/29 备用舵机（SERVO3/4）已按实装为 U9/U11。
> ⚠️ 两项打样疑点见 [README.md](README.md)：USBLC6 D1 pin5 接法、USB_DP/DM 线宽。
> 日期: 2026-08-04 | 主控: ESP32-P4C6-Core | 基于 Netlist 08-03 + [final-pinmap.md](final-pinmap.md)
> 新增: 摄像头(回归) / 六轴 / 舵机云台 / SD 卡 / USB 外设口 / 机械臂 / 电池充电 / 音频路由决策

## 一、总览

| 功能 | 器件 | 接口 | GPIO 数 | 电源 |
|------|------|------|---------|------|
| 摄像头 (回归) | OV5640 (MIPI 24pin) | MIPI-CSI 2-lane + I2C | 3 (MCLK/RST/PWDN) | 2.8V/1.5V (2 LDO) |
| 六轴 IMU | MPU-6500 (QFN-24) | I2C | 1 (INT) | 3V3 |
| 舵机云台 ×2 | SG90/MG90S | LEDC/MCPWM | 2 | 5V |
| SD 卡 | microSD 座 | SDIO 4-bit | 6 (+1 CD) | 3V3 |
| USB 外设口 | USB-A 母座 | USB 1.1 FS OTG (GPIO26/27) | 2 | 5V |
| 机械臂 | LeRobot SO101 (lerobortso101) | **USB-A 外设口** (控制板 CDC) | 0 | 控制板独立供电 |
| 电池 | 1S LiPo 2000-3000mAh | IP5306 充电+电源路径+5V升压 | 0~2 (电量) | 内置 |

## 二、GPIO 分配(新增)

| GPIO | 功能 | 复用/备注 |
|------|------|-----------|
| **GPIO5** | CAM_MCLK (OV5640 XVCLK, 24MHz) | 原 I2S_MCLK 孤立网络,改为摄像头时钟输出 |
| **GPIO12** | CAM_RSTB | 摄像头复位 (低有效) |
| **GPIO13** | CAM_PWDN | 摄像头掉电 (高=关) |
| **GPIO24** | SERVO_YAW | 云台水平 PWM (50Hz, 500~2500µs) |
| **GPIO25** | SERVO_PITCH | 云台俯仰 PWM |
| **GPIO26** | USB_DM | USB-A 外设口 D- (OTG_FS) |
| **GPIO27** | USB_DP | USB-A 外设口 D+ (OTG_FS) |
| **GPIO28** | SERVO3 | 备用舵机 (U9), 机械臂改走 USB 后释放 |
| **GPIO29** | SERVO4 | 备用舵机 (U11), 机械臂改走 USB 后释放 |
| **GPIO39** | SD_DAT0 | SD1_CDATA0 |
| **GPIO40** | SD_DAT1 | SD1_CDATA1 |
| **GPIO41** | SD_DAT2 | SD1_CDATA2 |
| **GPIO42** | SD_DAT3 | SD1_CDATA3 |
| **GPIO43** | SD_CLK | SD1_CCLK |
| **GPIO44** | SD_CMD | SD1_CCMD |
| **GPIO45** | SD_CD | SD1_CDATA4, 卡检测 (可选) |
| **GPIO47** | MPU_INT | 六轴中断输出 |
| GPIO33 | IP5306 KEY (可选软件开关机) | 需从模组引出方可软件控制; 不用则悬空 |
| I2C (GPIO8/21) | — | 共享: NFC(0x55) + 触摸(~0x24) + OV5640(0x3C) + MPU-6500(0x68) + (可选)MAX17048 电量计 |

> 占用的 39-45 均带 SD1/GMAC 复用,本设计不使用 SDIO 双卡/以太网,无冲突。
> 舵机 PWM 用 P4 的 LEDC(1 个)或 MCPWM(2 个)均可,50Hz 方波即可驱动。
> 机械臂已改走 **USB-A + 控制板** (见八节), 不占 UART/GPIO。

## 三、摄像头 — OV5640 MIPI (2026-08-04 回归)

> ✅ 2026-08-04 回归: 视觉方案重新纳入。接线不变, 新增 P4 NNA 说明。
> ✅ 2026-08-04 定案 (v2): 模块 **OV5640 MIPI 24pin 版** (0.5mm FPC, 带 AF-VCC 对焦马达供电脚, **无板载稳压**, 4 路电源外供)。取代早期 FYRK3399-30pin 方案。
> ✅ 2026-08-03 确认: 采购 **MIPI 版 OV5640 模块** (2-lane)。
> 手上 ESP32-S3-CAM 的 **DVP 24pin 模块不能接 P4 MIPI-CSI**, 弃用, 留作 S3 开发板使用。

### 模块参数 (2026-08-05 补充)

| 参数 | 值 |
|------|-----|
| 传感器 | OV5640, 500W (5MP) |
| 镜头 | 4P+IR (4 片玻璃 + 红外滤光) |
| 镜头规格 | **1/4"**, EFL **3.2mm**, F/No **2.6±5%**, 畸变 **<1%** |
| DVDD | **1.5V** |
| AVDD | **2.8V** |
| DOVDD | **2.8V** |
| AF-VCC (pin23) | 2.8V (对焦马达; 若为定焦镜头可一并接 2V8) |

> 与 LDO 方案吻合 (LDO1 2.8V / LDO2 1.5V), 无改动。
> F/2.6 + 1/4" + EFL 3.2mm 属低照度一般水平, 室内补光用 SK6812 键灯即可; 画质优先可后续换大光圈镜头。

### 接线 (24pin 定案, 2026-08-04)

| P4 信号 | → 模组 pin | 模组信号 |
|---------|-----------|---------|
| CSI_D0P (pin42) / CSI_D0N (pin41) | 20 / 22 | MDP0 / MDN0 |
| CSI_D1P (pin46) / CSI_D1N (pin45) | 12 / 13 | MDP1 / MDN1 |
| CSI_CLKP (pin43) / CSI_CLKN (pin44) | 16 / 18 | MCP / MCN |
| GPIO5 (CAM_MCLK, 24MHz) | 14 | MCLK |
| GPIO8 / GPIO21 (I2C0) | 3 / 5 | SDA / SCL |
| GPIO12 (CAM_RSTB) | 6 | REST (低有效) |
| GPIO13 (CAM_PWDN) | 8 | PWDNB (极性待确认) |
| +2V8 (LDO1) | 11 / 4 / 23 | DOVDD / AVDD / AF-VCC |
| +1V5 (LDO2) | 10 | DVDD |
| GND | 2,15,24 | AGND / GND / AF-GND (单点接平面) |
| STROBE | 1 | 闪光触发 (可选, 暂 NC) |
| NC | 7,9,17,19,21 | 悬空 |

- 6 根 MIPI 走线做 **100Ω 差分对、等长**,远离 5V/舵机/背光。
- OV5640 用 **2-lane 模式**,最高 1080p@30(或 720p@60)。
- 3 个地 (AGND/GND/AF-GND) 板上就近接**同一 GND 平面**, 无需切地。

### P4 NNA — 不只是拍照, 还能"看"

- ESP32-P4 内置 **NNA 神经加速器**, ESP-DL 支持人脸检测 / 手势 / 目标识别等模型 → 视觉可做**实时感知**, 不止取景拍照。
- 典型玩法: 主人在场/看镜头 → 表情情绪联动; 手势指令; 物体识别驱动对话。
- 固件: `esp_cam` (MIPI-CSI) + `ESP-DL`; `camera.take_photo` MCP 工具可参考 desk-robot 已有实现模式。

### 电源 (2026-08-04 定案: 无板载稳压)

规格书明确 **4 路电源全部外供**, 板上需加 2 颗 LDO + 2 颗磁珠:

| 模组轨 | 电压 | 供电 |
|--------|------|------|
| DOVDD | 2.8V | LDO1 (2.8V) 直供 |
| AVDD | 2.8V | LDO1 → 磁珠 FB 隔离 |
| AF-VCC | 2.8V | LDO1 → 磁珠 FB 隔离 (对焦马达) |
| DVDD | 1.5V | LDO2 (1.5V) 直供 |

```
+3V3 ── LDO1 ME6211A28 (2.8V/500mA) ──┬── DOVDD (pin11)
                                      ├── FB1 → AVDD   (pin4)
                                      └── FB2 → AF-VCC (pin23)
     └─ LDO2 AMS1117-1.5 (3.3→1.5V) ──→ DVDD (pin10)   TAB→+1.5V
```

- DVDD=1.5V 为 OV5640 允许上限(原厂 1.2V), 按模块规格走。
- LDO2 用 AMS1117-1.5 (3.3→1.5V 压差裕量 1.8V, 足够); 2.8V 必须用低压差 ME6211A28 (AMS1117 的 1.0V dropout 扛不动 3.3→2.8V)。
- 上电时序: RST 先低、rails 稳定后释放, 固件处理(见十五节)。
- 2.8V 轨总电流 ~150mA, 1.5V 轨 ~80mA, 两颗 LDO 余量充足。

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
- **电源**: 5V 直供 → 舵机座端 **470µF 电解 + 100nF** 就近缓冲。
  - 2 个 SG90/MG90S 轻载 ~400mA, 5V 轨足够。
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

## 七、USB 外设口 — USB-A FS OTG

> 2026-08-03 新增: 通用 USB Host 口, 用于电子设备 / 手机互连 / 低速摄像头 / U盘。
> 与调试口 (USB-C HS, pin48/49) **完全独立** — 走 FS_PHY2 (GPIO26/27), 零冲突。

### 接线

| P4 信号 | GPIO | → USB-A 母座 |
|---------|------|--------------|
| OTG_FS D+ | GPIO27 | D+ (pin3) |
| OTG_FS D- | GPIO26 | D- (pin2) |
| VBUS 5V | — | VBUS (pin1, 经限流) |
| GND | — | GND (pin4) + 外壳屏蔽 |

- USB-A 母座 (USB 2.0 规格), DP/DM 做 **45Ω 差分等长包地**, 远离背光 Boost 开关节点。
- VBUS 从板载 5V 取, 经 **500mA PPTC 限流** + **USBLC6-2SC6 ESD 保护**。
- 母座旁 100µF + 100nF 去耦 (外设浪涌)。

### 设备兼容

| 设备类型 | 协议 | 12Mbps (FS) 带宽 |
|----------|------|------------------|
| 电子设备 | CDC / HID | ✅ |
| USB 摄像头 | UVC MJPEG | ⚠️ 仅低分辨率 (720p 需 15~30Mbps) |
| 手机互连 | ADB / MTP | ⚠️ 慢但可用 |
| U盘 | MSC | ✅ (文件搬运/扩展存储) |

> **机械臂 (lerobortso101) 接此 USB 口** (FS OTG host + 控制板 CDC, 见"九、机械臂集成")。此口亦兼容 U盘/鼠标/低速摄像头。

### 软件 (2026-08-03 已确认)

- GPIO26/27 → FS_PHY2 → OTG_FS 控制器, ESP-IDF usb_host / TinyUSB 初始化时自动切 PHY。
- **ESP-IDF v5.5.1+ 完整支持 P4 双 USB-OTG 驱动**: FS 控制器可作为 host (`usb_host_install` API 选择 HS/FS 控制器)。本项目 v5.5.3 ✅
- **配置 (v5.5.3 源码已确认)**: `usb_host_config_t.peripheral_map = BIT1` 选择 **FS 外设** (index 1, GPIO26/27); HS 外设是 index 0。依据: `soc/esp32p4/usb_dwc_periph.c` (`controllers[1].internal_phy_io = {.dp=27, .dm=26}`)
- 同一时刻仅一个 OTG 外设能做 host (v5.5.x 限制); USB-C (Serial JTAG, 走 FS_PHY1) 是 device 角色, 与 FS OTG host **不冲突** ✅
- ⚠️ **打样后实测**: 接 FS 设备 (鼠标/U盘) 验证枚举 (TinyUSB RHPORT 选 FS, 或 usb_host 配置 FS 控制器)
- ⚠️ **P4 rev3 注意**: UTMI PHY 的 D+/D- 15k 下拉电阻需软件控制 (host 模式), 最终板芯片若为 rev3 需确认固件处理

---

## 八、机械臂集成 — LeRobot SO101 (lerobortso101)

> 2026-08-05 改走 **USB + 控制板**方案 (取代 08-04 的 UART1 直连舵机总线)。
> **关键**: SO101 舵机为 **SCServo 串行总线舵机**; 其控制板 USB 枚举为 CDC 串口, P4 通过 **USB-A 口 (FS OTG host)** 与其通信。控制板**独立供电**, P4 只发控制指令, 无需大电流。
> 直连总线 (1Mbaud UART) 仍可作为备选, 见下"协议"。

### 硬件接线 (USB)

| vbcpet | → SO101 控制板 | 说明 |
|--------|----------------|------|
| USB-A 口 (FS OTG host, GPIO26/27) | USB 数据线 | 控制指令 |
| — | 独立电源 (5V≥2A) | 控制板自带供电, 舵机电流不经过主板 |
| GND (可选) | 屏蔽地 | 共地 |

- **板上零新增元件**: 不需要 J_ARM / J_EXT5V。
- **GPIO28/29 保持 SERVO3/SERVO4** (U9/U11 备用舵机), 不再给机械臂。
- ⚠️ USB-A VBUS (500mA 限流) 仅作枚举/线缆检测; 舵机电流走控制板独立电源。若控制板固件会从 VBUS 取电超限, 可只接 D+/D- 不接 VBUS。

### 软件 (P4 USB Host)

- ESP-IDF v5.5.3 usb_host + **CDC-ACM 类驱动** (FS OTG host, `peripheral_map = BIT1`, GPIO26/27)。
- **协议: SO101 控制板固件协议 — 待确认** (参考 so101 Python SDK / 控制板源码)。
- 备选: 若控制板固件支持 USB 透传舵机总线 → 直接发下方 SCServo 帧。

### 协议 (SCServo 二进制帧 — 透传/直连模式用)

```
0xFF 0xFF | ID(1-6) | LEN | INST(0x03=写/0x02=读) | ADDR | DATA(小端) | CKSUM
CKSUM = ~(ID + LEN + INST + ADDR + DATA...) & 0xFF
```

控制表 (字节地址, 小端):

| 地址 | 含义 |
|------|------|
| 40 | 扭矩使能 (1=ON) |
| 42 | 目标位置 (2B LE, 4096步=360°) |
| 46 | 目标速度 (2B LE; 值 = speed(1-100) × 5) |
| 56 | 当前位置 (2B LE, 只读) |

关节限位 (步值 [CWmin, CCWmax], 0°=CW 极限) 与 HOME (desk-robot 2026-06-30 实测):

| 关节 | 限位步 | 角度范围 | HOME(度) |
|------|--------|----------|----------|
| 1 底座 | 1100-2900 | 97~255 | 92 |
| 2 大臂 | 800-3100 | 70~273 | 0 |
| 3 小臂 | 800-2990 | 70~263 | 179 |
| 4 腕转 | 950-3000 | 84~264 | 169 |
| 5 腕俯 | 800-3900 | 70~343 | 16 |
| 6 夹爪 | 2000-3400 | 176~299 | 32 |

角度→步: `steps = cw + (angle/180) × (ccw - cw)` (0-180° 线性)。
HOME_STEPS = {2019, 805, 2979, 2869, 1082, 2246}。

### 安全 (从 desk-robot 移植)

- 上电 / 急停后先 **EnsureTorqueEnabled()**: 逐关节读扭矩使能, 失能则补写 1。
- 关节2 < 15° 时, 限制关节1 摆动幅度 ±15° (防重心倾倒)。
- 关节3 < 20° 时, 禁止关节2 继续下降 (防夹)。
- `robot.arm.stop`: 释放全部扭矩 (机械臂自然下垂); `robot.arm.home`: 一键回 HOME。

### MCP 工具 (移植 desk-robot)

| 工具 | 作用 |
|------|------|
| robot.arm.move_joints | 6 关节绝对/相对角度, speed 1-100 |
| robot.arm.move_joint | 单关节微调 (含到位回读) |
| robot.arm.home | 归位到 HOME |
| robot.arm.stop | 急停 (释放扭矩) |

### 代码参考

- `D:\vscode\desk-robot\main\boards\xiaozhi-custom\xiaozhi_custom_board.cc` — SCServo 完整实现 (ScsWriteByte/Word/ReadWord, AngleToSteps, 安全锁, MCP 工具)。
- desk-robot 接线: S3 UART1 TX=IO9 / RX=IO10, 1Mbaud。
- 高带宽场景: 桌面视觉抓取 VLA (smolVLA) 由后端 GPU 推理 → 此方案只负责关节指令收发。

---

## 九、电池与充电

> 2026-08-04 新增。让桌宠摆脱"插电摆件", 可离线对话 / 被拿起 / 移动。

### 架构 (推荐: IP5306 单芯片)

```
USB VBUS (5V) ──→ IP5306 ──→ 5V_SYS 轨 (替换原 VBUS 节点)
                      ├─ 锂电充电 (1S 3.7V, 最大 2A)
                      ├─ 电源路径 (USB 优先; 无 USB 时电池升压 5V)
                      └─ 4-LED 电量指示 (LP1-4)
5V_SYS → MP2315(→3V3) / MAX98357A / SK6812 / USB外设口 / 舵机云台
```

- IP5306 输出恒 5V (USB 或电池升压), 自动切换 → **原 5V 拓扑零改动**, 各模块全保。
- 充电 1A (ISET 设), boost 输出上限 ~2.1A; 电池模式整机电流需固件管控。
- 电量: 4-LED 接板边 4 颗小灯 (免软件); 或 I2C 加 **MAX17048** 电量计 (走现有 I2C, 零额外引脚)。
- ⚠️ **IP5306 坑**: 空载/低载会自动关 boost (省电) → 需配置为**常开模式** (初始化按键时序), 或保持最小负载 / 用 KEY 脚。
- 电池: 1S LiPo 3.7V **2000-3000mAh, 必须带保护板** (过放 3.0V 截止); 18650 或聚合物按外壳空间定。
- 电池模式功耗管理 (固件): 降背光 / 舵机与 USB 外设口按需使能 / 关闭大负载 → 全功能约 1-2h, 待机数小时。

### 备选 (专业电源路径)

```
VBUS ──→ BQ24074 (电源路径 + TS 温控) ──→ SYSTEM ──→ TPS63020 buck-boost 3.3V@2A
                                              └────→ 5V boost (可选, 电池模式供舵机/USB)
```

- 无 IP5306 自动关机坑, 充电/温控更完整; 代价: 多 2-3 颗芯片, 3.3V 主轨改 buck-boost。
- 3.3V 主轨: USB 模式 SYSTEM≈4.4V / 电池模式=电池电压, TPS63020 全程稳压。

### 元件

| 位号 | 元件 | 封装/型号 |
|------|------|-----------|
| U_IP | IP5306 (充电+电源路径+5V升压) | ESOP-8 |
| BAT | 1S LiPo 2000-3000mAh + 保护板 | 18650 / 聚合物 |
| C_BAT | 100µF + 22µF + 100nF | 电池侧去耦 |
| (可选) U_G | MAX17048 电量计 | DFN-8, I2C |
| (备选) U_BQ | BQ24074 | QFN-20 |
| (备选) U_TS | TPS63020 | QFN-14 |

## 十、蓝牙/音频路由 — 无蓝牙硬件,音频走 WiFi 转发

> 2026-08-04 决策: 不加蓝牙元件,桌宠音频经 WiFi 推送到终端设备播放。

### 为什么不加蓝牙

| 硬件 | 无线能力 |
|------|----------|
| ESP32-P4 | 无任何 2.4G 射频 (纯算力 MCU) |
| C6 (模组内) | **WiFi 6 + BLE 5.0** (esp-hosted, SDIO/SPI 与 P4 互联) |

- **C6 只支持 BLE, 不支持经典蓝牙 (BR/EDR)** → 无法提供 A2DP/HFP → 蓝牙耳机无法直接连接桌宠。
- BLE Audio / LE Audio 目前不现实 (标准新、支持设备少、需 BT 5.2+)。

### 方案对比

| 方案 | 可行性 | 说明 |
|------|--------|------|
| 耳机同时连桌宠+终端 | ❌ | 需经典蓝牙模块; multipoint 耳机本质切源, 无法同时混两路 A2DP |
| **音频发终端播放 (采用)** | ✅ | 零新增硬件, 复用 C6 WiFi |

### 音频转发架构

```
桌宠 P4 ──I2S──> 本机喇叭 (MAX98357A, 独立工作)
        └─音频流─> C6 ──WiFi──> 手机/PC APP ──> 蓝牙耳机 (连终端)
                                              └─> 终端喇叭
```

- 耳机只连终端; 桌宠语音经 WiFi 推送, 终端混音播放 → 同一耳机同时听到桌宠+终端声音。
- 实时流 (Opus / TTS 直接推) 优于发文件; 局域网延迟几十 ms, 桌宠说话场景够用。
- 本机喇叭与转发可同时存在 (不戴耳机时直接听桌宠)。

### 固件注意

| 项 | 说明 |
|----|------|
| 音频传输 | P4 生成音频 → esp-hosted (SDIO) → C6 WiFi → TCP/UDP → 终端 App |
| 设备发现 | 桌宠 mDNS 广播, 终端 App 自动发现 |
| 触发入口 | NFC 触碰 → 打开 App → 自动连接音频流 (NT3H2111 FD 中断) |
| BLE 用途 | 仅作低带宽通道 (BLE HID / 手机近场唤醒), 不承载音频 |

---

## 十一、电源预算

| 新增负载 | 电流 | 电源轨 |
|----------|------|--------|
| OV5640 | ~200mA | 3V3 |
| MPU-6500 | ~3mA | 3V3 |
| microSD (读写) | ~50mA | 3V3 |
| 舵机云台 ×2 | ~400mA | 5V |
| USB 外设口 | ~500mA | 5V |
| 机械臂 | (外部电源, 不计入) | 独立 5V |

- **3V3 侧**: 原 1.2A + 0.25A ≈ **1.45A** < MP2315 2A ✅ (余量 27%)
- **5V 侧 (USB 供电)**: 原 0.56A + 0.9A ≈ **1.46A** < USB 5V/3A ✅ (余量 50%)
- **5V 侧 (电池供电)**: 整机全开 ~7.5W ≈ 1.5A < IP5306 boost ~2.1A ✅ (但 2000mAh 电池全功能约 1h, 需功耗管理降载)
- 机械臂控制板**独立供电** (5V≥2A), 不走主板 USB-A VBUS (仅 500mA, 作枚举检测)。
- 摄像头瞬态大, CSI 座旁加 10µF; 舵机座端 470µF; 电池路径加 100µF。

## 十二、新增元件清单

| 位号 | 元件 | 封装/型号 |
|------|------|-----------|
| U_CAM | OV5640 MIPI 24pin 模块 | 含镜头 |
| J_CAM | 24pin 0.5mm FPC 座 | FPC-24P 翻盖 |
| U_MPU | MPU-6500 | QFN-24 3×3 |
| U_SD | microSD 卡座 | 推入式 |
| J_SERVO | PH2.0 6pin 座 | 舵机云台×2 |
| U_IP | IP5306 (充电+电源路径+5V升压) | ESOP-8 |
| BAT | 1S LiPo 2000-3000mAh + 保护板 | 18650/聚合物 |
| (可选) U_G | MAX17048 电量计 | DFN-8, I2C |
| (LDO1) | 3V3→2.8V LDO (DOVDD/AVDD/AF-VCC) | ME6211A28 SOT-23-5 |
| (LDO2) | 3V3→1.5V LDO (DVDD) | AMS1117-1.5 SOT-223 |
| (FB1/FB2) | 磁珠 600Ω@100MHz | 0603 ×2 |
| C_SD | 100nF + 10µF | SD 供电去耦 |
| C_SERVO | 470µF 电解 + 100nF | 舵机电源 |
| C_MPU | 100nF + 1µF | MPU 去耦 |
| C_BAT | 100µF + 22µF + 100nF | 电池侧去耦 |
| J_USB | USB-A 母座 (USB 2.0) | 外设口, 卧式/立式 |
| F_USB | PPTC 500mA (JK16-500) | 0805, VBUS 限流 |
| D_USB | USBLC6-2SC6 | SOT-23-6L, DP/DM/VBUS ESD |
| C_USB | 100µF + 100nF | 1206 + 0603, USB VBUS 去耦 |
| J_EXP | 扩展排针 (GPIO28-33 + UART + 电源) | 2.54 双排针, 预留 |

## 十三、布局要点

1. **CSI 差分**: 6 线 100Ω 差分等长 (±0.5mm), 包地, 远离背光 Boost (12.8V 开关噪声)。
2. **摄像头座**放板边, 镜头朝外; FPC 走线短。
3. **舵机电源**粗线 (≥1mm), 470µF 靠近舵机座, 地与主板单点汇合。
4. **MPU-6500** 靠近板几何中心 (振动测量一致), 避开大电流/振动源。
5. **SDIO** 走线短等长, 与 MIPI 分区域。
6. **LDO (2.8V/1.5V)** 靠近摄像头座, 输出就近 1µF+100nF。
7. **USB 外设口**放板边, DP/DM 45Ω 差分等长包地; VBUS 粗线, PPTC 紧贴母座。
8. **电池**居中或底部, 远离天线/摄像头; IP5306 电感靠近芯片, 电池路径粗线 (≥1mm)。
9. **扩展排针**集中 GPIO30-33 + 3V3/GND, 便于杜邦线外接 (机械臂走 USB)。

## 十四、固件注意

| 项 | 说明 |
|----|------|
| 摄像头 | ESP-IDF **esp_cam** 驱动, OV5640 MIPI 2-lane, CSI 接口; + **ESP-DL** 做 NNA 识别 (人脸/手势) |
| CAM_MCLK | GPIO5 配置为 24MHz 时钟输出 (I2S MCLK / REF_CLK) |
| MPU-6500 | I2C 0x68, 轮询或 INT 中断读取 accel/gyro |
| 舵机云台 | LEDC 50Hz, 500~2500µs; 上电先复位到中位再使能 |
| 机械臂 | USB Host + CDC-ACM → SO101 控制板协议 (待确认); 控制板独立供电 |
| SD 卡 | sdmmc 驱动 4-bit SDIO, 25MHz, FAT 文件系统 |
| USB 外设口 | usb_host 驱动 OTG_FS (GPIO26/27); 鼠标/U盘/CDC-ACM (v5.5.1+ 支持 FS host) |
| 电池 | IP5306 常开模式配置; 电量 (4-LED 或 MAX17048); 电池模式降载策略 |
| 音频转发 | P4→C6 WiFi 推流 + mDNS 发现 + NFC FD 触发 App 连接 |

## 十五、待核实清单

- [x] **摄像头方向**: MIPI 版 OV5640 (24pin FPC 模块, 2-lane); S3-CAM DVP 24pin 模块弃用
- [x] **MIPI 版模块电源**: 无板载稳压, 需 2.8V/1.5V 双 LDO (2026-08-04 定案: ME6211A28 + AMS1117-1.5; 2.8V 必须低压差)
- [x] **MIPI 版模块 24pin 排线定义**: 已按规格书核对, 与 P4 CSI 1:1 对上 (2026-08-04); 08-05 改 24pin
- [ ] 舵机型号/尺寸 → 确认扭矩与电流 (决定 470µF 是否足够)
- [ ] microSD 卡座型号 (推入式) 与封装
- [ ] 触摸 I2C 实际地址 (与 MPU/OV5640 确认不冲突: 现规划 0x24/0x3C/0x68/0x55)
- [x] **FS OTG host 支持 (软件)**: ESP-IDF v5.5.1+ 支持 P4 双 OTG, FS 可做 host (2026-08-03 确认)
- [ ] **FS OTG host 实测**: 打样后接 FS 设备验证枚举; 确认模组芯片 rev (v3 需软件下拉)
- [ ] USB-A 母座型号/封装确认 (卧式或立式, 与外壳开孔匹配)
- [ ] **机械臂 USB 协议确认**: SO101 控制板 USB 枚举类型 + 固件协议 (CDC? 透传?); P4 端 usb_host_cdc_acm 适配
- [ ] **IP5306 常开模式配置**: 上电时序/按键配置流程确认; 或选 BQ24074+TPS63020 备选
- [ ] **电池选型**: 2000-3000mAh 具体型号 + 保护板; 外壳空间约束
- [ ] GPIO49-54 是否由模组引出 (扩展排针容量) — 需模组手册
