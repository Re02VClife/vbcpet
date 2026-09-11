# vbcpet 工作日志
## 2026-09-09 (星期三) — 🔧 屏驱动换版: kevincoooool 社区组件 → 屏厂 osptek 官方驱动 (3 处实质修正, 编译通过待烧录)

### 背景
- 屏 YDP430BT009-V1 (ST7102 480×800) 从未点亮过。此前 DSI 强制初始化 (`lcd`) 卡死在 `esp_lcd_new_dsi_bus` busy-wait, 当时归因 "屏未接/lane 未到屏"; 背光链路已毕业 (板端拓扑修复 + 屏内 LED 串损坏待换屏)。
- 屏与模组本就同厂 (鱼鹰光电 = OSPTEK), 屏厂在 gitee 开源了同型号屏的完整资料与示例: `gitee.com/osptek/4.3-tft-480x800-mipi-st7102` → `versions/YDP430BT009-V1/` (含 ST7102 datasheet、外形图、厂商 init 序列 `GX09C_ST7102+BOE4.3_2LANE_90HZ.c`、ESP32-P4 示例工程、树莓派5/_linux 设备树)。

### 对比结论: 社区组件与屏厂组件同源, 但固件集成层有 3 处实质问题
1. **DSI PHY 供电缺失 (头号嫌疑)**: ESP32-P4 的 VDD_MIPI_DPHY 必须由内部 LDO_VO3 输出 2.5V, osptek 官方示例在 `esp_lcd_new_dsi_bus` **之前**必调 `esp_ldo_acquire_channel(chan 3, 2500mV)`。旧固件完全没有这一步 → PHY 无电 → PLL 永不锁定 → `esp_lcd_new_dsi_bus` busy-wait 卡死 — 与之前记录的 "DSI HOST busy-wait" 症状完全吻合 (A6=0x500A0000 崩溃点)。
2. **PHY PLL 参考时钟选错常量**: 旧代码注释写 "显式用 XTAL 40MHz" 但实际写的是 `MIPI_DSI_PHY_PLLREF_CLK_SRC_RC_FAST` (内部 RC ~17.5MHz 高漂移, 不适合做 DPHY PLL 基准)。IDF 5.5.3 对 rev≥v1.3 的官方 DEFAULT 就是 XTAL。已改为 `MIPI_DSI_PHY_PLLREF_CLK_SRC_XTAL`。
3. **lane 速率 500 → 820 Mbps** (屏厂示例值, GX09C 2LANE 工程同款)。
4. (附) **init 序列换为屏厂专用序列**: 旧用的 kevincoooool 默认序列参数对应别的屏; 屏厂 GX09C 序列含本屏 BOE 4.3" 面板专用 VGH/VGL(0xB0=22 57..)/Source(0xB7=64 64)/VCOM(0xBF=B4 B4)/Gamma(0xC8/C9)/GIP(0xD7,A3,A6,A7,AC,AD) 参数, 0x11 退 sleep 延时 600ms (旧 120ms 过短), 尾部 0x35 (TE on)。

### 改动清单
- **vendored 屏厂组件**: `firmware/components/esp_lcd_st7102/` ← osptek 示例组件原样拷入, 仅 2 处 IDF 5.5.3 适配 (`color_space`→`rgb_ele_order` 改名; DPI 宏按 IDF 版本填 `in/out_color_format`)。见组件内 README.md。
- **依赖切换**: `main/idf_component.yml` 移除 `kevincoooool/esp_lcd_st7102`; `managed_components/` 里旧组件已删; `dependencies.lock` 已更新 (现仅 esp_codec_dev + idf)。
- **`display_pipeline_dsi.c` 重写**: +`dsi_phy_power_on()` (LDO VO3 2.5V, 建总线前) / lane 820Mbps / XTAL 基准 / `vendor_config.init_cmds` 传屏厂 GX09C 序列 / init 后显式 `disp_on_off(true)`。引脚/背光/SKIP 逻辑不变。
- **构建脚本**: `_build_osptek.bat` (需 `IDF_PYTHON_ENV_PATH=D:\Espressif\python_env\idf5.5_py3.11_env`, 否则 export.bat 会算错成不存在的 py3.14 环境; bat 内勿写中文注释 — GBK 代码页下乱码执行)。烧录 `_flash_osptek.ps1` (默认 COM10 = CH343, esptool 参数沿用已验证的 no_reset 重试循环)。
- **编译通过**: `vbcpet.bin` 0xE63E0 字节 (app 分区余 10%), BUILD_EXIT=0。

### 结果 ✅ 屏幕点亮 (2026-09-09 当日完成验证)
- **烧录链路澄清**: 板子串口是 **CH340=COM22**; 之前一直无应答的 COM10 (CH343) 是**别的设备**(误盯半天)。板载无 DTR 自动复位电路, `default_reset` 无效; 控制台僵死时唯一入口 = **strap (GPIO35→GND, GPIO36→3V3) + RST1** 进下载; 控制台活时 `dl` 免 strap。esptool 单进程 460800 `--before no_reset --after no_reset_stub` 沿用。
- **验证过程 (三轮烧录迭代)**:
  1. LDO VO3 OK → `phy_clk_src=XTAL` 直接 `abort()` — `mipi_dsi_ll.h:272`: P4 (rev<3.0) 的 DPHY PLL 参考时钟 mux **仅支持 PLL_F20M/RC_FAST/PLL_F25M, 无 XTAL** → 改 `PLL_F20M` (IDF 对 v1.3 的官方 DEFAULT; 之前 "F20M 锁不定" 实为 PHY 未供电)。
  2. PLL locked (史上首次!) → `new_dsi_bus ret=0` → 又卡死: 组件构造函数在 `esp_lcd_panel_reset` **之前**就读 ID (0x04 BTA 读), 而 LCD_RST(GPIO20) 上电浮空、屏躺在 reset 状态 → `mipi_dsi_hal_host_gen_read_short_packet` 无超时忙等 (A4=0x500a0000) → 加 `panel_hw_reset_early()` (RST 低 10ms→高→600ms, GX09C 时序) + bring-up 期间跳过 ID 读 (组件内 `#if 0`; LP 写事务不要求面板应答, 不会挂)。
  3. **终验全链路零卡死**: HW reset → LDO VO3 2500mV OK → PLL locked → `new_dsi_bus ret=0` → DBI IO → ST7102 panel → 屏厂 GX09C 序列 (Sleep Out 600ms → Display On → TE) → DPI 视频 + 渐变图案 → 背光 100% → **屏幕点亮, 显示紫红黄渐变 ✅** (用户实屏确认)。
- **结论**: 黑屏根因 = 驱动/固件层四项 (① DSI PHY 未供电 ② PLLREF 常量选错 XTAL·RC_FAST ③ 面板未复位先读 ID ④ init 序列非本屏参数), 换 osptek 官方驱动 + 上述修正后解决。8-24 "屏内 LED 串损坏" 结论作废 (灯珠正常, 已更正)。
- **遗留/后续**: ① 点屏已验证, 可恢复 ID 读做诊断 (组件 `#if 0` 处); ② 若后续图像异常 (花屏/偏色) 再查 FPC lane 交叉 (display_pipeline_dsi.c 头注); ③ GPIO48 1.2V IO 域驱动 MT3608 EN 的 V2 修改清单仍有效; ④ 控制台"僵化"固件 bug 待修 (本次多次复发, strap 是唯一可靠恢复烧录的路径)。

## 2026-08-24 (星期一) — 🔧 换新核心后烧录通道恢复: 根因 R12 开路(strap 上拉失效)

### 事件
- P4C6-Core 主控烧毁(5V 打地)后换新核心, V1 板重烧固件: 换芯后 esptool 全线 `No serial data`(115200/460800 均败, 多轮全灭) → 多轮排查 → **最终根因: R12 开路(4.7kΩ strap 上拉失效), 换新后烧录通道彻底打通**。

### 症状链与排查记录
- 电源实验: USB 电脑口供电 → 芯片 20ms 级 POWERON 循环复位; 换 2A 充电器 → 半稳(出现过两次干净 `waiting for download`); 再并 470µF 电解 → 完全静默。
- **触摸实验(关键线索)**: 手指摸 R11(10kΩ, CHIP_PU 附近上拉)→ 持续复位、ESP-ROM 乱码刷屏; 金属/电容引脚短碰 R11 → 单次干净复位 `boot:0x7 DOWNLOAD + waiting for download`。判定板上有高阻浮空节点, 人体耦合可触发复位。
- esptool 偶发握手成功一次: `Chip is ESP32-P4 (revision v1.3)`, 擦除 OK, 写入 65% 时 `The chip stopped responding`。
- 时序洞察: **0x45(`Invalid head of packet`, 收到 'E') ≠ No serial data** —— 0x45 = 链路通但 sync 撞上 ROM 打印流; 静默 ≠ 在下载等待(ROM 等待可能超时转 SPI 启动出厂固件, 出厂固件不打印 UART)。

### 根因
- **R12 = 4.7kΩ ±1% 0603 厚膜电阻**(LCSC `RC0603FR-0715RL` 类/阻值 4.7kΩ), 模组 strap/上拉网络成员。R12 开路 → 关键 strap 引脚浮空 → strap 采样失效 → 芯片从 SPI 启动(出厂固件静默) 或下载窗口时有时无, 表现酷似电源不稳。
- 修复: 更换 R12 → 上拉恢复 → 稳定进 `DOWNLOAD + waiting for download` → esptool 正常连接烧录。

### 结果 ✅ 烧录 + 启动确认
- 换 R12 后 esptool 一次通过: bootloader 23552B + partition 3072B + app 941056B @115200, 三镜像 `Hash of data verified`(写入耗时 92.3s)。
- 拔 strap 飞线后按 RST1 → `boot:0xf (SPI_FAST_FLASH_BOOT)` → ESP-IDF v5.5.3 bootloader → app 正常启动。
- 启动日志健康: PSRAM 32MB(200MHz), 按键×4 / SK6812×4 / Audio / USB-Serial-JTAG / 控制台全 ready, `Main loop` 运行中。
- 遗留旧问题(非本次引入): MPU-6500 WHO_AM_I=0x00(期望 0x70, U7 焊接疑点), SD 卡未插, Touch 控制器未探测。


### 背光链路大案 (2026-08-24 同日, 重大)
> 跨多轮排障, 最终破案: **V1 背光不亮 = 两个叠加原因: ① 板端 boost 拓扑错误 ② 屏内 LED 串损坏(疑似雷电4高压灌入事故遗留)。**

**① 板端拓扑错误 (根因一, 之前一直没抓到)**
- 从 V2 工程解析出真实网络: 电感 L3(10µH) 接在 **SW(pin1)──D2 之间**, 而 VIN(3V3) 直进芯片 pin5, **SW 与 3V3 之间没有电感** → 标准 boost 必须 `3V3─L─SW─D2─输出`; 原接法充电阶段电感无能量源, 芯片电流环路无法建立 → SW 恒 0V, 换多少颗芯片都不工作。
- 修复(V1 飞线): L3 一端从 D2 阳极改接 3V3, D2 阳极直接挂 SW → 上电 SW=0.9V(开关正常), 空载 LEDA=20V(升压能力 OK)。
- **MT3608 手册核对** (LCSC 官方 PDF 8 页): pin1=SW, pin2=GND, pin3=FB(0.6V), pin4=EN(High≥1.5V/Low≤0.4V, 不用时接 VIN 自动启动), pin5=VIN(2~24V), pin6=NC; UVLO=1.98V; 最大占空比 90%@VFB=0V。
- **GPIO48 之谜**: 官方核心板手册确认 pin76=GPIO48, 复用 SD1_CDATA7/GMAC_RXER = **SDIO/EMAC 1.2V IO 域**, 输出高仅~1V, 远低于 EN 阈值 1.5V → 用 GPIO48 驱动背光 EN 是引脚选择错误(之前误以为板上有漏电拉低)。GPIO46/47/49/50 同域, V2 必须避开。
- **R12 身份更正**: R12 = LEDK↔GND 恒流检流电阻(15Ω, 0.6V/15Ω=40mA), 之前误记为 strap 上拉电阻。更换 R12 使"下载恢复"或与 strap 无关, 待考证。

**② 屏内 LED 串损坏 (根因二)**
- 屏规格: 480×800 DSI, 背光 **8 LED 4串2并**, Vf≈12V@40mA。
- 实测插屏: LEDA=9.58V, LEDK=0.39V(≈26mA), 跨 LED 串 9.19V → 4 串 Vf(12V) 下不应导通, 但 26mA 存在+黑暗中微亮 → **部分 LED 损坏致有效 Vf 降至 ~9V**。
- 事故史吻合: 早期屏/板可能被 Thunderbolt 4 高压灌过, 屏背光从无点亮记录。
- 待办: 商家确认维修/换屏; 换屏后验证背光+DSI(`lcd`)。

**最终定案 (2026-08-24)**
- 电源对照实验: VIN=3.3V 正常(空载 LEDA=22V), VIN=5V(IP5306 VOUT) 完全不工作(SW 直通)——IP5306 5V 轨为"软轨"(USB 输入仅 4.6V、轻载打嗝、无输入电容), 与 1.2MHz 开关负载不匹配。**V1 定案: 背光用 3.3V 供电**(MP2315 硬轨+470µF), 足够满足 12V/40mA。
- 插屏验证: LEDA=9.7V / LEDK=0.4V(26mA 恒流) / 微亮 → 屏内 LED 串部分损坏(有效 Vf 降至 ~9.3V), **换屏**。板子背光链路毕业。
- 待办: 换屏后验背光+DSI(lcd); V2 背光修改清单(拓扑 L 位、BL_PWM 避开 GPIO46-50、LEDA 加输出电容, 供电路径别用 IP5306 VOUT)。
> **✅ 更正 (2026-09-09)**: 屏背光灯珠**复测正常**, 8-24 "屏内 LED 串部分损坏/微亮 → 换屏" 结论**作废** — 当时的 26mA/微亮读数更可能出现在恒流环非设计工作点, 黑屏主因就是**驱动/固件层问题**(DSI PHY 未供电、PLLREF 误选 RC_FAST、lane 速率、init 序列非本屏参数, 详见 2026-09-09 条目)。换屏计划取消, 直接烧新驱动验证。
### 教训(下次避免)
1. **strap/复位上拉电阻坏 ≈ 电源不稳的假象**: 症状全是复位循环/时序敏感/No serial data, 极易误判为供电问题。优先用万用表量 strap 引脚电压 + 查上/下拉电阻阻值, 别急着换电源。
2. 静默三态判读: (a) 稳定 `waiting for download` = 好, 立即烧; (b) 出厂固件 silent = 需复位进下载; (c) 复位循环间隙 = 假静默。
3. 调试时手指远离高阻上拉区(10k/4.7k 节点); 需要手动复位用金属镊子/电容引脚短碰, 比手指干净(手指=持续干扰刷屏)。
4. esptool 参数已定型: 单进程、`--no-stub`、115200、`--connect-attempts 30`、`--before no_reset`(CH340 无 DTR 自动复位); 烧录中途手勿碰板。
5. V2 板: 对 strap/CHIP_PU 上拉电阻(R11/R12 类)加测试点; 贴装后目检 + 万用表量 strap 电压再上电。

## 2026-08-18 (周一) — 状态灯 + 屏幕背光排障 + ⚠️ 主控烧毁(5V打地) + V2 路线图
> ⚠️ **事故**: 调试中 5V 线误碰板子 GND 打火 → 5V 主干塌到 4.2V → P4 核心 100°C+ → 3.3V 对地短路 → **P4C6-Core 主控模块 die 烧毁**(3.3V 轨已短路, 待换新模块)。
> 复盘: 板上 F1 实为 **0Ω 跳线(替保险丝)**, 主电源无真保险 → 浪涌直灌 P4。**下一代板设计教训已写入 [hardware/upgrade-roadmap-v2.md](hardware/upgrade-roadmap-v2.md)。**

### SK6812 状态灯 (完成)
- 4 颗按键背光 LED 兼任状态灯: 低亮度 7%, 蓝启动/绿ok/黄warn/红err/灭。
- 命令: `led <ok|warn|err|boot|off>`; `test all` 完自动按结果点亮(selftest 返回 P/F 计数)。实板五种颜色与熄灭全部验证通过 ✅。

### 屏幕背光排障 (未解, 主控已烧)
- 现象: 屏插着板子正常 boot(拖垮解除), 但背光 100% 不亮, DSI 强制初始化(`lcd`)卡死。
- 诊断: 新增 `bl`(背光隔离,不碰DSI) + `gp`(通用GPIO 驱动高/低/读回/释放) 命令。
- 读数: GPIO48(EN) 驱动高=仅1.2V(<MT3608 EN 门槛1.5V), 驱动低=0V, 读回=高; LED_A=0.08V(升压未起); EN网络对地16MΩ/对3.3V 4.2MΩ(悬空)。3V3轨 8.24k(健康)。线索指向 MT3608 焊反/EN引脚内部结构, 未能继续(事故中断)。
- 经验: DSI 初始化卡死 = 之前记录过的 DSI HOST busy-wait(esp_lcd_new_dsi_bus 起), 屏未接/lane 未到屏。

### 顺带修复
- `display_backend_init` 重构: SKIP 分支 + `display_backend_request_init()`(lcd 强制) + `display_backend_enable_backlight()`(bl 隔离) 拆分, 避免 SKIP 卡死。

### 下一步 (按 V2 路线图)
1. 换新 P4C6-Core 模块前: 先给 F1 换真 PPTC 保险 + 3.3V TVS + 查 IP5306/MP2315 有无被打伤。
2. 屏幕侧: MT3608 方向确认/重焊; DSI lane 顺序验证(需测试点)。
3. 固件: 修控制台"僵化"(空闲看门狗)。

## 2026-08-18 (周一) — 烧录管线打通 + dl 免strap + 外设状态澄清

### 核心突破: 烧录管线打通
- **根因**: strap "一直无效" 是复位方式问题 —— GPIO35/36 strap 完全有效, 需 **RST1 硬复位(CHIP_PU)** 而非断电重上电(板载 RST1 存在); 断电报 IP5306/时序下 strap 采样不干净。
- 手法: 跳线 GPIO35(pin62)→GND + GPIO36(pin63)→3V3 → 按 RST1 → `boot:0x7 (DOWNLOAD(USB/UART0/SPI)) waiting for download` → esptool 单进程 `write_flash` 成功(三镜像 Hash 校验)。
- 坑: ① esptool 两进程(read_mac 后 write_flash)会被 RAM-stub 破坏 → 单进程或全程 `--no-stub`; ② 板面控制台在长串口会话后偶发"僵化"(需 RST 恢复) — 待修固件问题。

### dl 软件免strap 验证成功
- 新增串口命令 `dl`: 置 LP_SYSTEM_REG_SYS_CTRL_REG(0x50110008) bit2 FORCE_DOWNLOAD_BOOT + esp_restart → 直接进下载模式, **零 strap**。
- 实测 `dl → boot:0x7 → esptool → DL-FLASH-OK hashes=3`; 全自动工作流(守望 app→发 dl→--no-stub 烧)跑通。**从此重烧=串口发 dl+esptool。**

### USB-Serial/JTAG 架构更正 (重要, 推翻旧结论)
- P4 的 USB-Serial/JTAG 在 **USB FSLS PHY0 = GPIO24/25 (USB1P1_N0/P0, 默认路由)**; **不在模组 pin48/49(USB2 HS UTMI, 那是板子 USB-C)**。
- 所以"USB-C D+ 不枚举 = 主控坏"是**误判** —— USB-C 归 USB2 OTG 且根本是另一条路; USB-C 上那两根是对的(0V 正常)。
- 验证: PC USB 信号线接 GPIO24/25 时 USJ 能枚举(PID_0012→1001 因构建而异); 但 app 的 servo_init(GPIO24/25) 抢焊盘 → 不稳定; MINIMAL_USJ 模式(不启 servo)下 USB 稳定。

### 外设状态澄清 (重要, 校正中间结论)
- **当前总线上没有 I2C 器件在用**: NFC 未焊、屏幕未插(touch 不在总线)、MPU-6500(U7) 焊了但疑似失败 → I2C 三项失败多为"器件未上板/未生效"。
- GPIO8(SDA)/GPIO21(SCL) 空闲均 3.25V(健康); `i2c_master_probe` 全超时 + 直读"全 0x00" → 属空总线/驱动 behavior, **非 SDA 拉死**。
- MPU WHO_AM_I=0x00 在 400k 与 100k 下均"成功读回 0x00" → U7 焊接/地址(AD0→0x68/0x69)待验证: 重焊后 `rd 68 75` / `rd 69 75`。

### 固件迭代 (已烧入/编译验证)
- 新增命令: `scan`(直读法, 不用 probe)/`rd <addr> <reg>`(原始 I2C 读, 打印 esp_err+值)/`reboot`/`dl`。
- I2C 速度 Kconfig 化降为 100kHz(I2C_FREQ_HZ, 兼容弱上拉); 背光空指针保护; secondary console 关闭; MINIMAL_USJ 本地开关已清。
- esptool/OpenOCD 助手: firmware/esp32p4-usj-flash.cfg + usj_flash.tcl + 根目录 _flash_*/_dlflash* 脚本。

## 2026-07-21 (周一)

### 代码整理与提交
- 将固件从单片 `vbcpet_main.c` 拆分为 7 个模块：transport, channel_demux, display_pipeline, interaction_engine, text_render, text_input
- 编译通过，0 error 0 warning
- 3 个 commit：firmware 重构、文档更新、PC 端推流+测试资源整理

### 集成验证
- 新模块固件烧录到 WT9932P4-TINY 开发板
- 全部 9 项集成测试通过：NVS、启动画面、engine hello、heartbeat、USB transport、demux 分流、display_pipeline 解码刷屏、端到端推流
- 7 个新模块，0 bug

## 2026-07-22 (周二)

### 硬件方案决策
- 决定使用鱼鹰 ESP32-P4C6-Core 核心板（替代 WT9932P4-TINY）
- 确认 C6 WiFi 通过 SDIO 连接 P4，吞吐 10x 余量（需求 5Mbps vs 实际 50Mbps）
- 音频方案：MAX98357A + INMP441，I2S 接口
- 显示兼容方案：SPI + MIPI DSI 双后端，Kconfig 编译时选择

### 显示后端抽象
- 拆出 display_backend.h 内部接口
- display_pipeline_spi.c：ST7789V 后端
- display_pipeline_dsi.c：MIPI DSI 占位
- Kconfig.projbuild：编译时选择，sdkconfig.defaults 默认 SPI

### Claude 交互桥 v0.1
- serial_bridge.py：Claude SDK ↔ COM16 ↔ P4 固件
- 实现文字交互闭环：PC 键盘输入 → Claude → 回复显示到 P4 屏幕
- 支持 /y /n /a 按键授权

### 中文字体支持
- tools/gen_font.py：从 SimSun 生成 16x16 位图 C 头文件
- font_cjk.h：7548 个字形，241KB
- text_render.c 新增 UTF-8 解码 + CJK 字形查找 + 渲染
- ASCII 字体保留内嵌

### PC 端提交
- serial_bridge.py、gen_font.py、中文字体相关提交

## 2026-07-23 (周三)

### serial_bridge.py v0.2
- 合并 Live2D JPEG 推流（原 stream_simple.py 功能）
- Claude 状态 → Live2D 表情动作映射（thinking/done/blocked）
- 数字按键 [1][2][3][4] 模拟 yes/no/yes-always/panel
- CTRL 消息通过 asyncio.Queue 串行化，避免并发写冲突
- on_status 从 async 修复为同步回调

### 显示稳定性排查（核心难点）
- **问题1**：统一 flush 的 g_buf_idx 初始值 bug → 第一帧前 flush 垃圾数据 → 花屏
  - 修复：feed_jpeg 内部 flush + text_render 独立 flush
- **问题2**：USB Serial JTAG TX/RX 冲突 → 心跳发送阻塞主循环
  - 修复：心跳改为非阻塞 send_ctrl_nb(timeout=0) + 加大 TX 缓冲
  - 最终方案：完全禁用 P4→PC TX，PC 端靠推流活动判断存活
- **问题3**：SPI DMA 队列溢出 → 每循环都 flush（1000次/秒）填满队列
  - 修复：只在有新帧时 flush（g_frame_decoded 标志）
- **问题4**（根因）：JPEG flush 的 SPI DMA 未完成时，text_render_box 写同一块内存 → DMA 读到半新半旧数据 → 花屏崩溃
  - 修复：改为单缓冲，先解码→叠加文字→一次 flush
- **问题5**：g_text_dirty 标志导致文字只渲染一帧 → 下一帧 JPEG 覆盖消失
  - 修复：去掉 dirty 标志，每帧都叠加文字
- **问题6**：draw_ascii/draw_cjk 把背景像素也画成黑色 → 整个字符变成黑色方块
  - 修复：改为透明背景，只画前景像素

### 当前状态
- Live2D 推流 + 文字交互实现（bridge v0.2）
- 文字渲染通路打通的最后一步修复中
- 鱼鹰核心板待购入后开始原理图设计

## 2026-07-24 (周四)

### 文字渲染通路打通
- **问题7**：text_render_box 只在 text_dirty=true 时渲染，第二帧 JPEG 覆盖 FB 后文字消失
  - 修复：去掉 text_dirty，每帧都重新叠加文字
- **问题8**：draw_ascii/draw_cjk 把背景像素也画成前景色 → 整个字符变成黑色方块而非字母
  - 修复：改为透明背景，`if (!glyph_bit) continue;` 只画前景像素
  - 白色矩形/全屏涂白/单字符 'A' 三级逐层诊断法确认：FB 写入通路 OK → 坐标 OK → 字符渲染 OK

### 串口协议稳定性
- **问题9**：行缓冲 256 字节限制 → 长消息（Claude 讲故事、天气报告）JSON 被截断，cJSON 解析失败
  - 修复：行缓冲 256→512→1024 字节
- **问题10**：串口写超时时 `\n` 可能丢失 → demux 永远等不到行尾，后续所有数据堆在同一行 → 状态崩溃
  - 修复：channel_demux 加 500ms 行超时自动重置保护
- **问题11**：PC 端截断 300 字符方案 + 100ms CTRL 延迟 + 停用 work_status/panel 下行消息，减少串口中断频率

### 滚动显示尝试
- 新增 text_render_count_lines() API 计算换行总行数
- 实现从底部开始显示（g_scroll_line = total_lines - CHAT_H）+ 2.5s 自动向上滚动 + 到底循环
- 遇到稳定性问题，暂时回退到最简显示前 3 行

### 当前状态
- ✅ Live2D 推流稳定
- ✅ 中文文字渲染正确（透明背景，显示在屏幕底部 3 行区域）
- ⚠️ 长消息/多次对话后偶发冻结，与 CTRL 消息批次数和时序相关
- ⬜ 滚动显示待稳定后重新实现

## 2026-07-27 (周日)

### Hiyori 模型修复 — 动作 & 表情

- **Motion 文件替换**：从 Live2D 官方 GitHub (CubismWebSamples) 下载 10 个真实 motion 文件 (8-19KB)，替换本地 214 字节 stub
  - 使用 jsdelivr CDN 绕过 raw.githubusercontent.com 网络问题
  - 验证为 Cubism 3 格式，31 条参数曲线，4.7s/30fps
- **表情系统**：Hiyori 官方无表达式文件（.exp3.json 为空），自己实现参数驱动表情
  - 调查发现 `@jannchie/pixi-live2d-display` 参数 API：`model.setParameterValue()` / `model.getParameterValue()`
  - **内部 API 陷阱**：`internalModel.parameterSetValue` 不存在，`internalModel.setParameterValueById` 也不存在 — 正确 API 在 top-level model 对象上
  - 实现 `EMOTION_PRESETS` + `applyEmotion()` 带 easeInOutQuad 500ms 过渡
- **创建 `tune_expressions.html` 可视化调参工具**：
  - 13 个面部参数滑块 + 5 个预设按钮 (Neutral/Happy/Sad/Surprised/Angry)
  - 解决 MIME type (.mjs→text/plain) → 自定义 HTTP server `extensions_map`
  - 解决 `window.PIXI` 未暴露 → `No Ticker` 警告
  - **核心难点 — 参数被 idle 动画覆盖**：
    - 尝试 `setParameterValue` → 被每帧 motion 覆盖
    - 尝试 `model.update() + app.render()` → 无效
    - 尝试 `updateNaturalMovements = no-op` → 只禁用物理，不影响 motion
    - 尝试 `motionManager.stopAllMotions()` → 独立 eyeBlink 子系统仍在跑
    - **最终方案**：`motionManager.update = no-op` + `setEyeBlinkEnabled(false)` → 参数全由用户控制
  - 用户用调参工具微调了 5 个表情的预设值（如 happy=闭眼笑 ^_^, angry=瞪眼咧嘴）

### 心情值系统 (Mood)

- **render.html**：右上角 `Mood:60` 显示，`window.setMood(v)` + `getMood()`
  - 0-25=angry, 25-45=sad, 45-65=neutral, 65-80=happy, 80+=surprised
- **serial_bridge.py**：心情引擎
  - Claude 状态偏移：done=+8, blocked=+5, editing=+3, running=0, thinking=-2, **error=-15**
  - 用户行为偏移：发消息=+3, 允许工具=+5, 拒绝工具=-3
  - 闲置回归：每 10 秒向基线 60 漂移 1 点
  - 命令行直接控制：`mood+20` / `mood-30`
  - Stream 输出：`[stream] 180 frames, 13.8 fps, 12KB/frame, mood:65`
- **表情生效的关键修复**：`applyEmotion()` 先 `stopAllMotions()` → 过渡参数 → 5 秒后恢复 idle

### 固件编译 & 稳定性

- **ESP-IDF 编译问题**：Git Bash (MSys) 中 idf.py 检测到 `MSYSTEM` 环境变量后只打警告不执行 main()
  - 修复：批处理中 `set "MSYSTEM="` 绕过检查
- 编译成功：8 个固件文件 + scroll display 逻辑，0 error 0 warning
- 烧录成功：ESP32-P4, COM16, 673KB
- **纯推流压测** (stream_test.py)：210 帧稳定，**13.8fps**, 12-14KB/帧

### Git 提交 (8 commits)

| Commit | 内容 |
|--------|------|
| `4bab522` | Hiyori motion stubs → 真实文件 + tune_expressions.html |
| `0f4c457` | firmware: 单缓冲 + 行超时 + 透明文字 + 滚动显示 |
| `0e81fef` | pc-host: serial_bridge v0.2 (推流+Claude+表情) + stream_test |
| `e60de58` | tune_expressions.html 调参修复 (禁用所有自动动画) |
| `7d2ab8b` | applyEmotion: im.parameterSetValue → model.setParameterValue |
| `38fc2d2` | 心情系统 (Mood 0-100, 右上角显示, 自动漂移) |
| `c79180c` | mood+/- 命令行测试指令 |
| `0947fda` | applyEmotion 先停 idle motion 再设表情 (关键修复) |

### 当前状态

- ✅ Hiyori 动作正常（真实动画 8-19KB motion 文件）
- ✅ 表情可控（参数驱动，5 个预设，用户已调参）
- ✅ 心情系统完整（Claude 事件 → mood 偏移 → 表情映射 + 右上角显示）
- ✅ 固件稳定（单缓冲 + 行超时 + 滚动显示，13.8fps 推流）
- ✅ 调参工具可用（`live2d/tune_expressions.html`）
- ⬜ 需 P4 硬件连接验证：serial_bridge.py 端到端 + 心情表情联动

## 2026-07-28 (周一)

### 心情 → 表情持续影响系统 (动作队列 v3)

**背景**：旧方案 `applyEmotion()` 停止 idle → 过渡表情 → 5 秒后恢复 idle → **表情被动画覆盖消失**。需要心情持续影响角色面部。

**尝试过的方案**：

1. **motionManager.update() hook** — 每帧动画后混合心情参数
   - 复用 `motionManager.update` 在动画更新后叠加表情参数
   - 问题：PIXI v8.19 + `@jannchie/pixi-live2d-display` 1.3.0 + Cubism Core 5.1.0 存在底层 WebGL 缓冲冲突
   - `bindBuffer: element array buffers can not be bound to a different target` × 1635 错误
   - 根因：Cubism SDK 的 `doUpdateMotion` → `updateParameters` 与 PIXI v8 的 WebGL 状态管理冲突
   - `tune_expressions.html` 正常是因为禁用了所有动画；有动画持续跑就触发

2. **脉冲动画方案** — 80% 时间静态表情 + 20% 动画脉冲
   - 禁用持续 idle，用 `setTimeout` 周期性短暂播放 2.5s 动画
   - 问题：需要禁用原生眼眨+自然运动（否则仍触发 WebGL 错误），需手动眨眼
   - 用户要求保留原生系统

3. **动作队列方案 (最终)**：
   - 保留原生眼眨 + 自然运动（WebGL 错误可接受，仅在动画播放期出现）
   - 拦截 `startRandomMotion`，手动控制队列节奏
   - 循环：播放 idle → 轮询 `isFinished()` → idle 结束 → 应用心情表情 2.5s → 下一 idle
   - 表情参数直接 `setParameterValue` 控制（无 motion 覆盖）
   - 平滑过渡 `_transitionMoodParams`：easeInOutQuad, 300-620ms

**render.html 最终架构**：
```
startRandomMotion 拦截 → _motionQueueLoop()
  ┌──────────┐     ┌──────────────┐     ┌──────────┐
  │ Idle ~4.7s│ → │ 表情 2.5s    │ → │ Idle ~4.7s│ → ...
  │ (原生眨眼) │    │ (mood 表达式) │    │ (原生眨眼) │
  └──────────┘     └──────────────┘     └──────────┘
```

**serial_bridge.py 调整**：
- 移除 `_apply_l2d_status` 中冗余的 `_l2d_motion("Idle")` 调用
- 心情引擎、Claude 状态偏移、mood± 指令保持不变

### 抗锯齿尝试

用户反馈 Live2D 锯齿严重：
- MSAA (`antialias: true`) → 无效
- SSAA (`resolution: 2`, 480×640 内部渲染) → 无效
- 根因：模型在 240×320 画布上 `scale: 0.16` 仅渲染 ~76×107 像素，细节不足 2 像素宽
- 结论：抗锯齿技术无法解决超小渲染的锯齿，需调大模型或重构图，暂搁置

### 硬件 BOM 全部定稿

BOM 原本 5 个 ⚠️ 待确认项全部解决：

| 项目 | 决定 |
|------|------|
| 显示屏 | **YDP430BT009-V1** (鱼鹰光电), 4.3" IPS, 480×800, ST7102 MIPI-DSI 2-lane + 触摸 |
| PA 功放 | **NS4150**, GPIO 控制, 3W D 类 |
| 在位检测 | **PN532 NFC** (I²C), 手机放底座自动识别 + 触发配对 |
| PD 供电 | **CH224K** 请求 9V/2A, 5V 回退; 板载 3.3V Buck + 1.8V LDO |
| 主控 | **ESP32-P4C6-Core** (鱼鹰光电), 88pin 邮票孔, P4+C6 一体模组 |

### 硬件文档产出

- **[hardware/final-pinmap.md](hardware/final-pinmap.md)** — 最终板完整引脚分配
  - 基于 P4C6-Core 88pin 逐个信号落实到 GPIO
  - DSI / I²C / I²S / 触摸 / 按键 / NFC / LED / USB / 调试串口
  - 供电架构：USB-C 9V PD → 3.3V Buck + 1.8V LDO + 背光升压
  - I²C 地址表：ES8311=0x18, ES7210=0x40, PN532=0x24
- **[hardware/devboard-pinmap.md](hardware/devboard-pinmap.md)** — 当前开发板 WT9932P4-TINY 实际引脚占用与兼容性分析
- 解析了 2 份 PDF 数据手册：YDP430BT009-V1 (屏幕) + ESP32-P4C6-Core (模组)

### 关键发现

- **GPIO14-19 在 P4C6-Core 上未引出**（内部 PSRAM/Flash 占用）
  - 当前固件的 IO16(BCLK)、IO18(SCL)、IO15(DOUT) **不可用**
  - 最终板改为：SCL→IO21, BCLK→IO22, DOUT→IO23
- **ST7102 驱动有 ESP-IDF 官方组件** (`espressif/esp_lcd_st7102`)，兼容性好
- **触摸 I²C 为 1.8V**，需 TXS0102 电平转换，直接接 3.3V 会烧 IC
- **ESP32-P4C6-Core 模组和 YDP430BT009-V1 屏幕同厂**（均为鱼鹰光电），供应协同有优势

### 当前状态

- ✅ 心情持续影响表情（动作队列 v3，原生眼眨+自然运动保留）
- ✅ BOM 全部定稿，5/5 ⚠️ 已解决
- ✅ 最终板完整引脚分配
- ✅ 调参工具可用（`live2d/tune_expressions.html`）
- ⬜ 固件引脚适配（IO15/16/18 → IO21/22/23）
- ⬜ 开始绘制原理图 (KiCad)
- ⬜ P4 硬件端到端验证

## 2026-07-29 (周二)

### 原理图设计文档

- 编写 [schematic-design.md](hardware/schematic/schematic-design.md) — 5 页完整设计，逐元件逐脚连接表
- 生成 [bom-lcsc.csv](hardware/schematic/bom-lcsc.csv) — 立创商城 BOM 含 C 编号，可直接导入嘉立创 EDA
- 更新 [schematic/README.md](hardware/schematic/README.md) — 嘉立创 EDA 专业版绘制指南

### 设计修正（用户审查 + 自主发现）

**供电架构修正**：

| # | 问题 | 原来(错) | 修正 |
|---|------|----------|------|
| 1 | P4C6 封装 | 2×44 双排 | **22×4 四面引脚** (邮票孔核心板) |
| 2 | 背光电压 | 26V (推算) | **12.2V/40mA** (从 PDF 第 8 页提取) |
| 3 | NS4150 供电 | VBUS 9V (超规格) | **+3V3** (最高 5.5V) |
| 4 | CH224K CFG | GND (实际=15V) | **+3V3** (请求 9V) |
| 5 | Buck 芯片 | SY8088 (最高 5.5V) | **MP2315** (4.5-24V 宽压) |
| 6 | MP3302 供电 | VBUS 9V (超规格) | **+3V3** (最高 6V) |

**CH224K 讨论**：WT9932P4-TINY 开发板用最简单 USB 5V→3.3V 稳压，没用任何 PD 芯片。总功耗 ~5W，USB-C 5V/3A=15W 绰绰有余。CH224K 暂时保留但可去掉。

**USB 通信**：确认 P4 内置 USB Serial JTAG + DFU，单根 USB-C 线搞定供电+串口+烧录+JTAG 调试，与开发板一致。无需 CP2102/CH340/FT232 等桥接芯片。

### 音频架构重设计

用户指出小智项目用 MAX98357A + INMP441 全数字方案 VS 我设计的 ES7210+ES8311+NS4150。

**旧方案 (废弃)**：
```
ES7210(ADC) + 模拟 MEMS → I2S TDM → P4
P4 → I2S → ES8311(DAC) → NS4150(PA) → 喇叭
```
- 3 颗 IC + 2 颗 I2C 地址 + 模拟走线 + AGND 分区 + 磁珠

**新方案 (全数字 I2S)**：
```
P4 I2S0 ↔ MAX98357A (I2S DAC+3W PA) + ZTS6672×2 (I2S 数字麦)
```
- **MAX98357A** (CH98357 国产替代, ¥6)：I2S 直入, 内置 D 类 3W, 滤波免输出
- **ZTS6672 ×2** (ICS-43434 平替, ¥3/颗)：I2S 数字 MEMS 麦, 灵敏度-26dB, SNR 65dB
- 删 3 颗 IC (ES7210/ES8311/NS4150)
- 删 6 颗阻容 (隔直/滤波)
- 无模拟走线, 无 AGND 分区, 无磁珠
- I2C 总线上只剩 NT3H1101 + 触摸 (原 4 设备)
- BOM 净减少 8 行

**ZTS6672 引脚 (从 EDA 库验证)**：7 脚, 无 MCLK — {LRC, SELECT, GND, BCLK, VDD, DATA, CONFIG}。文档中 8 脚带 SCK/MCLK 的描述**全部错误**（用户画图时发现）。

**不需要 MCLK**：ZTS6672 和 MAX98357A 都支持从 BCLK 内部生成时钟。GPIO5 释放。

### BOM 优化

| 改动 | 旧 | 新 | 理由 |
|------|----|----|------|
| NFC | PN532 (¥15+) | **NT3H1101** (¥2, I2C 0x55) | 只需手机触碰开 APP，数据走 C6 WiFi |
| 保险丝 | PTC 1206 2A | **0Ω 跳线 + TVS** | 开发板同方案，简单可靠 |
| 音频 | ES7210+ES8311+NS4150 | **MAX98357A+ZTS6672×2** | 全数字 I2S，少 3 颗 IC |

### 用户开始绘制原理图

- 在嘉立创 EDA 专业版中建 P4C6-Core 模组符号 (M1), 已接好大部分引脚
- 放置了 CH224K, MP2315, CH98357, ZTS6672, FPC30, 按键等
- **发现 ZTS6672 引脚与文档不符** (7 脚 vs 8 脚, 无 MCLK)
- **CH98357 接线需修正**：DIN→I2S_DOUT (非 IO7), LRCLK→I2S_WS (非 IO16), BCLK→I2S_BCLK (非 IO15)

### 当前状态

- ✅ 心情持续影响表情（动作队列 v3）
- ✅ BOM 全部定稿 + 多轮修正
- ✅ 最终板完整引脚分配
- ✅ 原理图设计文档 (5 页)
- ✅ BOM CSV (31 行)
- ✅ 音频架构重设计 (全数字 I2S)
- ✅ 模块化设计方案
- 🔧 原理图绘制中 (嘉立创 EDA)
- ⬜ 固件引脚适配

## 2026-07-30 (周三)

### 原理图绘制 + 设计迭代

用户在嘉立创 EDA 中画原理图，过程中发现多处理论与实物不符，逐项修正。

### 关键修正

**1. ZTS6672 引脚错误**
- 文档写成 8 脚带 SCK/MCLK，实际 ZTS6672 是 **7 脚，无 MCLK**（用户从 EDA 库中确认）
- ZTS6672 和 MAX98357A 都能从 BCLK 内部生成时钟，GPIO5(MCLK) 释放

**2. 触摸 IO 是 3.3V，不是 1.8V**
- PDF 解析偏差导致误判。用户按官方文档建 FPC 符号，TP_VDD 标 3.3V
- 删掉整个 1.8V 供电路径：ME6211C18、TXS0102、TXS0101 ×2、C7/C8
- 触摸 4 个引脚直接 3.3V 连 P4 GPIO

**3. 1.8V 不存在 → 全部文档重写**
- 6 个文档更新：bom.md、final-pinmap.md、power-tree.md、schematic-design.md、bom-lcsc.csv、README.md
- final-pinmap.md 完全重写，schematic-design.md 完全重写

**4. 音频架构迭代**
- 原设计 ES7210+ES8311+NS4150 → 改为 MAX98357A+ZTS6672×2 全数字 I2S
- 最终：MAX98357A 外购模块 + ZTS6672 外购模块 ×2
- 无 MCLK 需求，I2S 总线只需 BCLK/WS/DOUT/DIN

**5. 其他选型修正**
- PN532 → NT3H1101 → **NT3H2111** (NXP 升级版, 更便宜, pin2pin)
- 自恢复保险丝 → **0Ω 1206 + TVS**
- CH224K PD → **去掉** (USB-C 5V/15W 远超总功耗 4.2W)
- 轻触开关 → **Cherry MX 轴体** ×4
- WS2812B 3535 ×1 → **5050 ×4** 键下级联 RGB
- 背光 MP3302 → **MT3608** (更常见好买)
- Buck SY8088 → **MP2315** (宽压 4.5-24V)

### 模块化方案

整理出 [modular-design.md](hardware/modular-design.md)：

```
主板元件(板上):
  P4C6-Core + MP2315 + MT3608 + FPC30 + NT3H2111
  + Cherry MX×4 + WS2812B 5050×4 + RST
  + 上拉电阻 + 去耦电容

外购模块:
  MAX98357A I2S 功放模块 + ZTS6672 I2S 麦模块 ×2 + IPEX1 天线
```

主板 BOM 31 行（含外购 4 行），对比全分立 30 行但主板焊点从 ~50 降到 ~35。

### 文档产出

- [modular-design.md](hardware/modular-design.md) — 模块化设计完整方案
- 重写 [schematic-design.md](hardware/schematic/schematic-design.md)
- 重写 [final-pinmap.md](hardware/final-pinmap.md)
- 重写 [power-tree.md](hardware/power-tree.md)
- 更新 [bom.md](hardware/bom.md)、[bom-lcsc.csv](hardware/schematic/bom-lcsc.csv)、[block-diagram.md](hardware/block-diagram.md)
- 用户画出 39 个元件，通过网表交叉验证接线

### 当前状态

- ✅ 全部硬件文档同步
- ✅ 模块化方案定稿
- ✅ 引脚分配最终确认
- 🔧 原理图绘制中 (39/全量元件已放, 缺 MP2315/MT3608 等电源部分)

---

## 2026-07-31 (周四)

### 按键+RGB 方案变更: Cherry MX → 凯华轴座 + SK6812 3215 反贴灯

用户提出将按键和 RGB 方案从"Cherry MX 轴体直接焊板 + WS2812B 5050"改为可拔插的凯华热插拔轴座 + 3215 反贴灯方案。

**凯华轴座 6-pin 定义:**
| Pin | 信号 | 说明 |
|-----|------|------|
| 1 | VDD | LED 供电 (5V VBUS) |
| 2 | DOUT | LED 数据出 (→下一灯 DIN) |
| 3 | GND | LED 地 |
| 4 | DIN | LED 数据入 (←前灯 DOUT / GPIO51) |
| 5 | K0 | 开关端子 0 (→GPIO+上拉) |
| 6 | K1 | 开关端子 1 (→GND) |

**SK6812 3215 供电分析:**
- VDD_min=3.5V, 3.3V 供电蓝色通道不亮
- **改为 VBUS (5V) 直供**, 与 MAX98357A 共享 5V 轨
- 3.3V GPIO 直驱 5V SK6812: 实测可行 (输入阈值 ~2.5V), 保守可串 1N4148 降 VDD 至 ~4.3V
- **限流电阻: 不需要** (SK6812 内置恒流驱动, RGB 每通道独立恒流)
- 仅需: 数据线 33Ω 匹配电阻 + 每灯 100nF 旁路 + 总线 100µF

**接线方案:**
```
LED 级联: GPIO51─[33Ω]→LED1_DIN, LED1_DOUT→LED2_DIN, LED2_DOUT→LED3_DIN, LED3_DOUT→LED4_DIN(DOUT=NC)
按键: SW1-4 K0→GPIO1-4(+10kΩ 上拉), K1→GND
供电: 4灯 VDD 全部接 VBUS(5V) + 100µF 总去耦
```

### PCB 布局布线指南

用户询问 PCB 布线注意事项。编写了完整的 [pcb-layout-guide.md](hardware/pcb-layout-guide.md)，涵盖:
- **MIPI-DSI 差分对**: 100Ω 差分阻抗, 对内等长 ≤0.25mm, 同层不换层, 包地
- **C6 RF 天线**: 50Ω 单端, <15mm, 0 过孔, 圆弧拐角
- **USB DP/DM**: 90Ω 差分, FS 不严格
- **I2S**: ≤20mm 等长, 远离 DC-DC SW 节点
- **MP2315/MT3608 电源**: SW 节点最小化, FB 远离 SW, 输入电容紧贴 IC
- **NFC PCB 天线**: 3~4 匝 25×25mm, 下方禁铺地
- **4 层板推荐**: Top(Sig) / L2(GND) / L3(Pwr+Sig) / Bot(Sig)
- 嘉立创 EDA 操作要点

否决了用户调整 MIPI-DSI 引脚映射的想法 (时钟/数据物理通路不可互换)。

### D:\vscode 权限修复

整个 `D:\vscode` 目录 Users 组只有 RX 权限无 Write 权限，导致 Write 工具持续 EPERM。
执行 `icacls D:\vscode /grant Users:F /T` 修复。

### 文档全面更新

更新文件清单:
- [bom.md](hardware/bom.md): NT3H1101→NT3H2111, 轻触开关→凯华轴座, WS2812B→SK6812 3215, MP3302→MT3608
- [bom-lcsc.csv](hardware/schematic/bom-lcsc.csv): 新增 凯华轴座/SK6812/100nF×4/100µF/33Ω 行, BOM 30→35 行
- [schematic-design.md](hardware/schematic/schematic-design.md): Sheet 05 全重写, 全局 NT3H1101→NT3H2111, MP3302→MT3608, WS2812B→SK6812
- [schematic/README.md](hardware/schematic/README.md): 全量更新, 新增 SK6812/轴座/布线约束/pcb-layout-guide 引用
- [final-pinmap.md](hardware/final-pinmap.md): 按键/状态灯章节全重写, 供电架构更新
- [modular-design.md](hardware/modular-design.md): 按键/LED 模块描述重写, 系统框图更新, BOM 30→35
- [power-tree.md](hardware/power-tree.md): 全量重写 v1.1, SK6812 从 +3V3 移到 VBUS, MT3608 替 MP3302
- [block-diagram.md](hardware/block-diagram.md): Cherry MX→凯华轴座, WS2812B→SK6812
- **[pcb-layout-guide.md](hardware/pcb-layout-guide.md)** (新建): PCB 布局布线完整指南

### 当前状态

- ✅ 按键+RGB 方案改为凯华轴座 + SK6812 3215 反贴灯
- ✅ PCB 布局布线指南就绪
- ✅ 全部硬件文档已同步最新方案
- 🔧 原理图绘制中 (EDA 中需更新 Sheet 05 为新的轴座+反贴灯方案)

## 2026-08-02 (周日)

### 屏背光与供电核实 (YDP430BT009 手册)

pdftotext 成功提取屏手册全文, 确认背光 **8 LED 4S2P**, IF=20×2=40mA, Vf=10.8~13.6V (typ 12.2V)。
- **R5 背光采样电阻 5.1Ω→15Ω** (MT3608 恒流 0.6V/15Ω=40mA)
- 屏 VDD = 3.0~3.6V (typ 3.3V) → FPC VDD3.3V 直连 3V3, **无需 LDO**
- 触摸 I2C 标称 1.8V 电平, 3.3V 直连可行

### 全模块 datasheet 排查 (修正 3 处)

- **MP2315**: FB 基准确认 **0.8V** (非 0.6V) → 分压 100k/22k 改 **40.2k/12.7k** (原会输出 4.44V); VCC=0.1µF, BST=100nF+20Ω
- **MAX98357A**: GAIN 接 GND = **12dB** (原标 3dB 错误)
- **NT3H2111**: FD 开漏, 需 **4.7k 上拉** (原设计漏)
- 确认无误: MT3608 (FB 0.6V), ZTS6672 (SELECT 分声道), SK6812 (5V 直驱), 模组 88pin 与 pinmap 一致

### 接线方案文档

- 新建 [connection-fix.md](hardware/schematic/connection-fix.md): 逐脚接线方案 + 元件变更表

## 2026-08-03 (周一)

### 原理图网表复核 (断线全部修复)

- Netlist 08-03 复核: MP2315/MT3608/背光/屏 FPC/NFC 全部接好
- P0 修复: R4 接通 VBUS→+5V; SK6812 VDD 并入 +5V (5V 网络更名 +5V)
- P1 修复: 新增 IPEX U4 (LAN_OUT); I2C 上拉 R16/R17 (4.7k)
- 剩余 P2: I2S_MCLK 孤立 (GPIO5 可释放); GAIN=15dB (确认)

### 外设拓展方案

- 新建 [expansion.md](hardware/expansion.md): OV5640 MIPI 摄像头 + MPU-6500 六轴 + 舵机云台×2 + microSD (SDIO 4-bit)
- GPIO 分配: GPIO5(MCLK)/12(RSTB)/13(PWDN), 24/25(舵机), 39-44(SD), 47(MPU_INT), CSI 41-46
- 摄像头接口确认: ESP32-S3-CAM 摄像头是 **DVP 24pin**, 不能接 P4 MIPI-CSI → 弃用, 换 **MIPI 版 OV5640**
- 电源预算: 3V3 新增 ~250mA (总 ~1.45A < 2A); 舵机走 VBUS 5V + 470µF

### 外设拓展实装 (原理图, 08-03 下午)

**USB-A 外设口** (方案定稿 → 实装):
- USB2 (USB-A 母座 QHHTZB) + F1 (PPTC 500mA/16V) + D2 (USBLC6-2SC6)
- **FS OTG host 验证**: ESP-IDF v5.5.3 支持 P4 双 OTG, `usb_host_config_t.peripheral_map = BIT1` 选 FS 外设 (GPIO26/27, 模组 pin53/54); 源码依据 `soc/esp32p4/usb_dwc_periph.c` (controllers[1].internal_phy_io = dp27/dm26)
- 数据线用独立网络 **USB_A_DP / USB_A_DM** (不占用 HS 调试口 USB_DP/USB_DM)

**其他外设实装**:
- MPU-6500 (U7): I2C 0x68 + MPU_INT→GPIO47 + REGOUT 去耦
- microSD (TF1): SDIO 4-bit (GPIO39-44) + CD (GPIO45)
- 舵机 ×4 (U8-U11): SERVO1_YAW/GPIO24 + SERVO2_PITCH/GPIO25 驱动; SERVO3/GPIO28 + SERVO4/GPIO29 **预留** 3 线接口
- 功放 U1→**U12** (CH98357): 全 11 连接 (DIN/GAIN=GND/SD_MODE/BCLK/LRCLK/VDD=5V/OUTP/OUTN)

**网表审查修复**:
- USB2 D+/D- 网络合并: I/O1/I/O2 → USB_A_DP/USB_A_DM (USBLC6 pin1/6 与 pin3/4 内部合并)
- USB2 D+/D- **极性交叉**修复
- 功放重接 (原 U1 仅 GND)

**网表工具**:
- [tools/schematic_snapshot.py](tools/schematic_snapshot.py): 解析 epro2 (zip→epru, 按 DOCHEAD 分段取 SCH_PAGE/PCB) → 元件清单 + PAD_NET 网络
- [tools/schematic_review.py](tools/schematic_review.py): 自动审查 USB 极性 / 悬空网络 / 关键元件连接完整性

### 当前状态

- ✅ 原理图外设拓展实装 (USB 口 + MPU + SD + 舵机×4), 摄像头搁置
- ✅ 全板网表审查通过 (USB 极性/悬空/元件完整)
- ✅ 网表实时解析 + 自动审查工具就绪
- ✅ 文档同步: PROGRESS / WORKLOG / expansion / final-pinmap / BOM
- 🔧 PCB 布局; 摄像头待 MIPI 模块 pinout

## 2026-08-04 (周二)

### VS Code 顶栏主题 (DeepSeek 深蓝)

- 项目级标题栏改为 DeepSeek 深蓝 (#4D6BFE): [.vscode/settings.json](.vscode/settings.json) `workbench.colorCustomizations`
- 仅本工作区生效, 不影响其他项目

### 无线充电 & 蓝牙评估 (两项否决)

- **NFC 无线充电**: NT3H2111 无 Qi 充电能力, 仅毫瓦级能量采集 (VOUT) → 不做
- **蓝牙音频**: C6 仅 BLE 5.0 (无经典蓝牙 → 无 A2DP/HFP), 耳机双连桌宠+终端不可行
- **定案**: 不加蓝牙硬件, 音频走 **WiFi 转发** (P4→C6→TCP/UDP→终端) 由终端混音播放 (含蓝牙耳机), 零硬件成本

### 外设拓展方案 — 4 项

| 拓展 | 方案 | 资源 |
|------|------|------|
| 电池+充电 | **IP5306** (充电2.1A/升压2.4A/1µH) + **1S LiPo 3.7V** (4.2V, 保护板) | VBUS 轨改由 IP5306 VOUT 供 |
| 毫米波雷达 | **HLK-LD2410B** (人体存在/接近) | UART2 GPIO30/31 + OUT GPIO32 |
| 视觉回归 | **OV5640** MIPI 30pin + P4 **NNA** (人脸/手势识别) | CSI 41-46 + GPIO5/12/13 |
| 机械臂 | **LeRobot SO101** SCServo 总线 (1Mbaud UART) | UART1 GPIO28/29, 外部 5V 独立供电 |

- 协议参考 desk-robot: SCServo 二进制协议, 4096 步=360°, JOINT_LIMITS/HOME_STEPS
- 产出: [expansion.md](hardware/expansion.md) v1.3 (16 节) + [final-pinmap.md](hardware/final-pinmap.md) (GPIO28-32 新增) + [expansion-modifications.md](hardware/schematic/expansion-modifications.md) v1.0 (原理图修改方案, 改电源→摄像头→毫米波→机械臂顺序)

### 摄像头方案对比定案 → OV5640

对比 OV3660 / GC2093 / OV5647 / IMX219 / Zero-W 22pin:

| 结论 | 理由 |
|------|------|
| P4 ISP 上限 1080p | 高像素传感器全浪费, IMX219 8MP 无意义 |
| **IMX219 / GC2093 不在官方 esp_cam_sensor 支持表** | 需自移植驱动, 打样板风险高 → 弃 |
| OV5647 官方支持但仅 RAW | 无硬件 JPEG, 拍照费 CPU → 弃 |
| **OV5640 官方支持 (MIPI, YUV422) + 模块生态成熟** | ✅ 定案 |

### 摄像头规格书定案 (FYRK3399-OV5640-100FPC, 仁恒影像)

- 单页矢量 PDF 无文本层, 用 OCR 提取 (30pin 引脚表由用户核对提供)
- 传感器 OV5640 5MP, 自动对焦 (AFVDD 存在), 4P+650IR 镜头 (F/2.6, EFL 3.29mm, 畸变<1%)
- FPC 30pin 0.5mm 长 100mm, 双面电磁膜接地, 180° 弯折
- **⚠️ 无板载稳压, 4 路电源外供**: DOVDD/AVDD/AFVDD **2.8V** + DVDD **1.5V**
- **电源方案更新**: 原"3V3 直供 or 1.8V LDO"作废 → 新增 LDO1 ME6211A28 (2.8V) + LDO2 ME6211A15 (1.5V) + FB1/FB2 磁珠
- **引脚表核对**: MIPI lane0/lane1/时钟与 P4 CSI **1:1 直连零交叉**, I2C/控制脚全部对上, GPIO 分配零改动
- **方案变更 (v2)**: 摄像头改 **OV5640 MIPI 24pin** 模块 — 连接器 30P→24P, 电源轨不变 (DOVDD/AVDD/AF-VCC 2.8V + DVDD 1.5V), LDO 方案沿用; 新增 STROBE (闪光, 可选 NC), 地分 AGND/GND/AF-GND (单点接平面), PWDNB 极性待确认
- 文档更新: expansion.md (接线表+电源+待核实清单), expansion-modifications.md (改动总览/BOM/网络表), bom/power-tree/final-pinmap 同步 24pin

### 当前状态

- ✅ VS Code 顶栏主题 (DeepSeek 深蓝)
- ✅ 无线充电/蓝牙两项否决, WiFi 音频转发定案
- ✅ 外设拓展 4 项方案定稿 (电池/毫米波/视觉/机械臂), 原理图修改方案就绪
- ✅ 摄像头 OV5640 + 规格书定案 (无板载稳压 → 2 LDO + 2 磁珠)
- 🔧 待用户按 expansion-modifications.md 改图, 改完跑网表复查

## 2026-08-05 (周三)

### 摄像头 24pin 定案 + 网表审查

- 模块参数补充: OV5640 24pin, 镜头 4P+IR (1/4" F2.6 EFL3.2 畸变<1%), DVDD 1.5V / AVDD / DOVDD 2.8V → LDO 方案吻合, 零改动
- DRC 审查: LDO2 确认 AMS1117-1.5 (ME6211A15 找不到); **2.8V 必须 ME6211A28 低压差** (AMS1117 的 1.0V dropout 扛不动 3.3→2.8V); LOD2.8V GND(pin1)需接、AMS1117 TAB→VOUT 散热
- 网表 08-05 审查: IP5306/LDO1/LDO2/L3/电池/CN1 已画; 发现并反馈 **3 处**:
  - 🔴 **IP5306 VIN 接错**: VIN=VOUT=+5V (USB-C VBUS 未拆出 VBUS_IN) → 电池电路短路
  - 🔴 **CSI 时钟对未接**: M1:43/44 ↔ CN1:16/18 (MCP/MCN) 全是孤立网
  - 🟡 **MCLK 双网名**: M1:20=CAM_MCLK vs CN1:14=MCLK 未连通
- 数据 lane / RST / PWDN / I2C / +2V8 / +1.5V / 电池 均已接对 ✅

### 机械臂改走 USB (方案 B)

- 决定: 机械臂 **不再占用 GPIO/UART**, 直接接 **USB-A 口 (FS OTG host)** + SO101 控制板 (独立供电, P4 只发指令)
- 板上 **零新增元件**: 砍掉 J_ARM / J_EXT5V; GPIO28/29 保持 SERVO3/SERVO4 (U9/U11 备用舵机), 消除 SERVO 冲突
- 软件: P4 usb_host + CDC-ACM → SO101 控制板协议 (⚠️ 待确认)
- 文档同步: expansion.md (九节重写/GPIO/待核实), expansion-modifications.md (改动4/BOM/网络), final-pinmap/bom/block-diagram/power-tree

### 电源修复 (MP2315 EN) + 毫米波弃用

- 网表复查: 3 处反馈全部修复 (IP5306 VIN→VBUS_IN、CSI 时钟、MCLK) ✅
- 新发现 P0: **MP2315 EN (pin6) 误接 VBUS_IN** → 电池模式 Buck 关闭整板断电; 已改回 **+5V** ✅
- 全板审查通过, 剩余悬空均为有意项 (LAN_OUT 删 U4 后留空 / IP_LED1 / GPIO33)
- **毫米波弃用** (2026-08-05): LD2410B 方案取消 — 删除 expansion 毫米波节/GPIO30-32/BOM(U_MM/J_MM), 相关文档同步; GPIO30-32 释放为通用空闲脚
- **U4 删除**: 模组自带天线座, 板载 IPEX 冗余 → LAN_OUT 留空

### 全局深审 + 8 项修复

- 全量网表逐子系统核对, 发现并反馈:
  - 🔴 **+5V/5V 两个网分裂** (只靠 R4 桥接) → 合并为 +5V
  - 🟡 **缺失支撑电容**: BAT+ (C_BAT)、VBUS_IN (10µF)、+2V8/+1.5V 输出、舵机 470µF
  - 🟡 **SD CMD/DAT 无外部上拉** (软项, 可固件内部上拉)
  - 🟡 AMS1117-1.5 TAB 未接 (散热) / MAX98357A GAIN 悬空 (=12dB)
- 用户修复全部到位: 5V 合并、C4(VBUS_IN)、C1/C2/C3(BAT+)、C5/C6(+2V8)、C7/C9(+1.5V)、AMS1117 TAB✅

### 位号重排 + 审查工具改造

- 全板**位号重排** (左上→右下): 功放 U12→U4、麦 U3/U5→U5/U6、舵机重排
- [schematic_review.py](tools/schematic_review.py) 改为**按 part_id + 网络定位** (不再写死位号): 重排后全项通过 ✅
- 新增审查项: 摄像头座 13 个关键网络、电池 IP5306 连接
- 发现 LOD2.8V 实为 **XC6206P282MR (150mA)** — 对焦瞬间逼近限流, 已知晓

### PCB 布线指南 v1.1

- [pcb-layout-guide.md](hardware/pcb-layout-guide.md) v1.1: 新增 **MIPI-CSI 差分**、**IP5306 电池路径**、**摄像头 LDO** 布局节
- 修正: RF (U4 删→模组自带 IPEX)、NFC (匹配元件不接)、MP2315 引脚/阻值、线宽表 (BAT+/+5V ≥1mm)、磁珠例外 (摄像头 FB1/FB2)

### 当前状态

- ✅ 摄像头 24pin 定案 (参数/接线/电源)
- ✅ 机械臂改 USB + 控制板 (零硬件新增)
- ✅ 网表全绿 + 位号重排完成, 审查工具与位号解耦
- ✅ 毫米波弃用, GPIO30-32 释放
- ✅ 8 项电源/去耦修复到位, PCB 布线指南 v1.1
- 🔧 待查: SO101 控制板 USB 协议
- ➡️ 下一步: **PCB 布局**

## 2026-08-06 (周四)

### 屏 FPC 引脚号大修 (DSI 差分走法核对)

- 用户发现布局指南"推荐走法"与实际接线不符 → 逐网表核对:
  - 08-03→08-05 期间屏 FPC 整片重新对线, 文档仍停在 08-03 旧图
  - 实际网表: **D0→FPC 13/14, D1→16/17, CLK→19/20** (旧 11/12/14/15/17/18 作废)
  - 模组 M1 侧 DSI 引脚与 datasheet 一致 ✅ (34=D1P, 36=CLKN, 37=CLKP, 38=D0P)
- 同步 4 处文档: pcb-layout-guide v1.2、final-pinmap (6 引脚号)、schematic-design (整张 FPC 30pin 表重写)、expansion 两处

### CSI 引脚纠错 + 网表反相发现

- 前一晚误按网表改 expansion 文档的 CSI 引脚号 → 查模组 datasheet 发现**网表反了**
- datasheet 权威: **41=CSI_D0N, 42=CSI_D0P, 45=CSI_D1N, 46=CSI_D1P**
- 网表模组符号 D0/D1 两对 **P/N 反接** → 文档改回 datasheet; 网表待用户决策 (改符号 or 固件 lane 极性翻转)

### DSI 差分交叉分析

- 模组顺序 D1/CLK/D0 vs 屏座 D0/D1/CLK → **循环错位, 至少一对必须绕线** (结构性, 非接错)
- 两类交叉: 对内 P/N 镜像 (D0/D1, 极性翻转或毫米弯, <0.5mm) + 对间 D1↔CLK (C 形绕线, <5mm skew)
- 模组引脚物理固定不可改 (鱼鹰成品 88pin 邮票孔) → 交叉在布局层绕, 写入指南

### 模组几何 + 扇出确认

- datasheet PDF: **25×25mm, 88 脚周边邮票孔, 焊盘中心距 ~31mil (0.8mm), 间隙 ~10mil**
- 扇出法: 每脚 4mil 细线垂直扇出 3~5mm → 开阔区汇成差分对; 周边脚无需从焊盘间挤过

### 路由数据审查 (08-06 首版布线)

- 读到用户存盘路由: D1 对 461.0/461.3mil、各 2 过孔、10mil 宽 — 与用户描述一致
- 已走 11 网 (DSI 全 6 + CSI 的 D0/D1 4 网 + LEDK); **CSI 时钟对 CLKP/CLKN 未走**, USB/电源未走
- 问题: 全部 MIPI 10mil (默认宽)、**D1/CLK 走上 Inner1 (=GND 平面)**、8 个过孔无 GND 缝合、**未设差分对**

### 100Ω 差分参考值大修正 (核心)

- 官网阻抗计算器 (tools.jlc.com) 实测 JLC04161H-7628: **线宽 8.8mil / 间距 5mil → 100Ω**
- **旧指南 4.5/8mil 作废** (7628 介质厚 8.28mil, 4.5mil 宽偏到 ~130Ω); pcb-layout-guide → **v1.3**
- 量用户实际走线: 线宽 10mil ✓接近 8.8, 但 **线距太宽 ~10~29mil** → 差分阻抗 ~110-116Ω 偏高 (目标 100±10%)
- 建议: 8 对设差分对, MIPI 按 8.8/5mil 走 Top 层, D1/CLK 从 Inner1 撤出

### 差分对清点 + NC 检查

- 整板 **8 对差分**: DSI 3 (100Ω) + CSI 3 (100Ω) + USB 2 (90Ω)
- NC 网短接 7 脚 (屏 FPC 7/11/22/23/25/28 + 摄像头 CN1:1 STROBE) → 建议 STROBE 单独悬空

### 当前状态

- ✅ 屏 FPC 引脚表全文档对齐 (与网表一致)
- ✅ 100Ω 差分参考值修正为 8.8/5, 指南 v1.3; 交叉/扇出/几何已写入
- 🔧 待做: 模组符号 CSI P/N 反相接法 (改符号 or 固件补偿); 用户设差分对 + 按 8.8/5 重走 MIPI + D1/CLK 撤 Inner1
- ➡️ 下一步: 差分对定义 + MIPI 重走 + 间距验证

## 2026-08-07 (周五)

### 阻抗参考值大修正 (指南 v1.4)

- 官网阻抗计算器实测 **JLC04161H-7628 通用免费款** (L1-L2 单层 7628 8.28mil, 1.1mm core):
  - **100Ω 差分 = 线宽 7.3mil / 间距 6mil**
  - **90Ω 差分 = 线宽 9.5mil / 间距 6mil**
- **08-06 的 8.8/5 作废** (那是 7628B 特殊款, 不适用); pcb-layout-guide → **v1.4**

### 8 差分对重走

- **DSI 3 对 + CSI 3 对**: 7.3mil/6mil, 100Ω
- **USB 2 对** (USB_DP/DM 调试 + USB_A_DP/DM 外设): 9.5mil/6mil, 90Ω
- 全部走 Top 层, 无过孔跳层; 对内等长 ≤0.5mm
- 旧板 (Board1) 差分核实通过

## 2026-08-08 (周六) — 旧板打样前审查 + 元件答疑

### 旧工程 (vbcpet20260721) 审查

- 🔴 **L2 Buck 电感足迹错误**: 4.7µH@2A 功率电感塞不进 0603 焊盘 → 已打板, **bring-up 需飞线** (CD54 大电感焊引线到 0603 盘), 下版改功率电感足迹
- ⚠️ 旧工程残留 **自动布线 Board2** (PCB3, 全 6mil, 走无效 Inner7/8 层) → **勿打样, 只导出 Board1**
- SD 卡修正: R8 从串联改 **10k 上拉到 3V3** (pin9=SD_CD), pin6=VSS→GND
- IP5306 KEY 确认 = GPIO33 (pin60, 与 datasheet 核对)

### 元件答疑 (选型确认)

- **电感电流**: L1(IP5306, 1µH)≥2.5A / Isat≥3A, L2(Buck, 4.7µH)≥2A, L3(Boost, 10µH)≥0.5A; **开关电源电感不可用 50mA 件或直连**
- **USBLC6-2SC6** 参数确认 (±30kV ESD, 0.8-1pF, Vrwm 5V); 替代料核对通过
- 差分线宽线距表: MIPI 7.3/6, USB 9.5/6; 对间距 ≥3 倍线宽
- 去耦电容可换值/电解电容替换原则

### 电源线宽审查 (旧板)

- 主干达标: +5V 40mil, BAT+ 45mil, VBUS_IN 30mil, 3V3 30~60mil

## 2026-08-11 (周二) — 新工程「桌面开发助手」+ 打样

### 新工程审查

- 工程重命名 **桌面开发助手**; **单 PCB 文档** (旧 Board2 已清除)
- ✅ **L2 足迹修正**: 0603 → **IND-SMD_L7.4-W6.6** 功率电感 (焊盘 98×130mil) — 08-08 的 0603 问题解决
- ✅ SD 卡: pin6=GND, pin9=SD_CD+R8(10k 上拉) 全对; 显示 FPC / 摄像头 CN1 / M1 67 脚全对上
- ⚠️ **发现 2 处**:
  - **D1 (USBLC6) pin5 = VBUS_IN** — 应为 **+5V**: 电池模式 VBUS_IN=0V → USB-A 口 D+/D- 被钳到 ~0.7V, **USB-A 外设口电池模式瘫痪**
  - **USB_DP/DM (USB-C 调试口) 线宽 7.3/10mil** — 应为 **9.5mil** (90Ω); 7.3 是 MIPI 的 100Ω 宽度

### 电源线宽复审

- 主干全部达标: BAT+ 45mil, VBUS_IN 30mil, +5V 40mil, 3V3 30-60mil
- ⚠️ LED_A 全网络 10mil (应 ≥11.8), +2V8 大部分 10mil; +5V 有 58mm / 3V3 有 139mm 的 10mil 细段 → 建议加宽

### 打样 + 文档清理

- **已下单嘉立创打样** (2026-08-11)
- ⚠️ **待核实**: 盘上 08-11 工程网表 D1 pin5 仍=VBUS_IN、USB_DP/DM 仍 7.3/10mil —— 若打样件与此网表一致, 这两点会带到板子上, bring-up 重点验证
- 文档清理: 删旧网表 .enet×3、升级方案.md×2、bom-lcsc.csv (外设拓展前/位号错)、KiCad pcb/ 占位、旧 epro2×5; 修正 schematic/README + modular-design 接线表 + bom.md CH224K/J2 注释

## 2026-08-11 (周二) — 板到货前固件开发 (最终板 P4C6-Core)

在等板期间并行开发最终板固件,目标焊完当天能用。

### M1 显示重构 + DSI 后端

- **分辨率/格式解耦**: `LCD_W/H/BUF_SIZE` 硬编码 → **Kconfig 化** (`VBCPET_DISPLAY_W/H/FORMAT`); 帧缓冲 480×800 RGB565=768KB → **PSRAM** (`CONFIG_SPIRAM=y`)
- **DSI 后端实现** (`display_pipeline_dsi.c`): MIPI-DSI 2-lane ST7102 480×800, 用社区组件 **`kevincoooool/esp_lcd_st7102`** (含完整 init 序列 + 480×800 DPI 配置宏); 背光 LEDC PWM GPIO48; LCD_RST GPIO20
  - 屏手册确认: ST7102 TDDI, 触摸默认 0x55 (与 NFC 冲突→探测); 触摸由 LCD_RST 控制 (勿动 TP_RST)
  - ⚠️ ST7102 MIPI lane 顺序交叉 (模组 vs FPC) — 板到后对线, 必要时寄存器调

### M2 外设驱动 (全部, 编译验证)

- `board_config.h`: 最终板引脚唯一权威
- `peripherals/`: **i2c_bus** (共享 I2C GPIO8/21) + **buttons** (GPIO1-4, 消抖+上报 PC) + **sk6812** (RMT WS2812B ×4) + **touch** (ST7123 探测式) + **audio** (MAX98357A TX + ZTS6672×2 RX, I2S0 全双工 48kHz) + **mpu6500** (0x68) + **sd_card** (SDIO 4-bit+FAT) + **servo** (LEDC 50Hz GPIO24/25) + **nfc** (0x55+FD) + **usb_host** (FS OTG `peripheral_map=BIT1`, 枚举检测)
- 删除过时 `audio_codec.c/.h` (ES7210/ES8311)

### M3 bring-up 框架

- `bringup.c`: UART0 控制台 (`help/list/test <name>/test all/brightness`); 启动逐个初始化外设 (容错)
- `self_test.c`: 每外设 `xxx_self_test()` 注册, 打印 PASS/FAIL — 焊完逐项跑

### M4 PC host 480×800

- `render.html`: 画布 240×320 → **480×800 可配** (`?res=WxH`), 模型 scale 随分辨率
- `serial_bridge.py` / `serial_stream.py`: `--res WxH` 参数 (默认 480x800, 开发板 240x320)
- `transport_usb.c` RX 缓冲 16KB→32KB

### 构建基础设施

- `_idf.py` 包装器: **Git Bash 下 idf.py MSYSTEM 跳过 main 的 workaround** (纯原生 python 剥 MSYSTEM) + 工具链 env (ESP_ROM_ELF_DIR 等)
- 双板配置: `sdkconfig.defaults` (最终板 DSI+PSRAM) + `sdkconfig.defaults.devboard` (SPI); `_build.bat` / `_build_dev.bat` 独立 build 目录
- **编译通过**: 最终板 DSI 全量 + 开发板 SPI 双验证

### 文档

- `firmware/README.md` 重写 (实际状态); `docs/webrtc-bringup.md` 新建 (WebRTC 板到后清单); PROGRESS 更新
- WebRTC: `webrtc_skeleton.c` 未编译骨架 (esp-hosted + esp_webrtc + VPU, 板到后启用)

## 2026-08-12~16 — 板到货 + 元件焊接

- ✅ 打样板到货, 元件焊接完成
- MP2315 忘买 → bring-up 阶段用 **mini360 模块顶替** 出 3.3V (后补购)
- 之前记录的「短路」实为**供电 bring-up 问题** (IP5306 无电池 → VOUT 不出 5V), 非 PCB 短路

## 2026-08-17 (周一) — 最终板 bring-up: 上电 + 固件卡死定位

### 供电 bring-up (全链路打通)
- IP5306 无电池时 VOUT 仅 **1.94V** (充电直通不升压); 接 1S 锂电后 **4V**; KEY(GPIO33) 短接唤醒后 **5V**
- mini360 (顶替 MP2315) 出 3.3V, 实测 **3.48V** (范围内, 可接受)
- 四路电源全部确认: **5V / 3.48V(3V3) / 2.79V(摄像头 2.8V) / 1.49V(摄像头 1.5V)** ✅

### 板子 boot 确认 ✅
- 模组正常 boot + 固件运行 (UART0 控制台经 CH340 = COM6 可见)
- 但固件**卡死**: task_wdt 每 5 秒触发, `main` 任务忙等

### 根因定位 (寄存器级确认)
- 崩溃 dump: `A6 = 0x500A0000` = **MIPI_DSI_HOST** (经 `esp32p4.peripherals.ld` 核对)
- MEPC ≈ 0x4007a6fc 落在 IROM (flash 映射代码), 紧循环轮询 DSI 状态位
- 结论: `esp_lcd_panel_init()` 卡死 — **屏未接 / DSI lane 顺序问题** (文档早前标注的待核实项)

### 屏幕连接问题 (次要, 明日继续)
- 插屏后板子**连 ROM 日志都不打** (完全不 boot) → 屏 FPC 有短路/接触不良, 插上拖垮板子
- 屏 VDD↔GND 电阻正常 (无静态短路), 但带载拖垮 → FPC 错位/接触问题待查
- 拔屏后崩溃循环恢复, 证明**板子本身正常**

### 固件改动: 跳过显示 (让外设+控制台先跑起来)
- 新增 `CONFIG_VBCPET_SKIP_DISPLAY` (Kconfig + sdkconfig.defaults + display_pipeline_dsi.c)
- `display_backend_init()` 跳过 DSI, `panel=NULL`, 上层 (display_pipeline/text_render) 已做 NULL 容错
- **编译成功** (`build/vbcpet.bin`)

### 烧录受阻 (明日待办)
- **USB-C 不枚举**: 电脑上连「未知 USB 设备」都没有 → **D+ 物理不通** (查 A6/B6↔pin49 虚焊/线)
- **UART0 烧录**: 需手动进下载模式 (GPIO35=0 + GPIO36=1 + 复位), 无人值守做不了
- 固件已就绪, 只差烧录 → 烧进去即可 `test all` 验证外设
