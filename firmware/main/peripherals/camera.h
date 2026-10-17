/** OV5640 (MIPI-CSI, CN1) bring-up —— 2026-09-28
 *
 * 本阶段**只做探测**, 不起 CSI/ISP:
 *   1. 用 LEDC 在 CAM_MCLK(GPIO5) 上出 24MHz (OV5640 的 XVCLK)
 *   2. 走正确上电时序: PWDN 有效 → RSTB 拉低 → 释放 RSTB → 等稳压/时钟稳
 *   3. 用已有共享 I2C(GPIO8/21) 读 SCCB 地址 0x3C 的 PID 寄存器 0x300A/0x300B
 *
 * 为什么不能只 `scan`: CN1.6(CAM_RSTB)/CN1.8(CAM_PWDN) 上**没有任何上拉/下拉电阻**
 * (2026-09-28 查 .enet 网表: 这两条网只连 M1 和 CN1 两个脚), 复位后是浮空输入,
 * OV5640 停在未定义态; 且 GPIO5 复位后不是时钟输出 ⇒ 没有 XVCLK 的 OV5640 不会
 * 应答 SCCB; 所以"scan 里没有 0x3C"单独不能证明模块坏。
 *
 * 寄存器依据 (官方 HPM SDK hpm_ov5640.h, 2026-09-28 核对):
 *   OV5640_I2C_ADDR              0x3C
 *   OV5640_CHIP_ID_HIGH_BYTE_ADDR 0x300A / VALUE 0x56
 *   OV5640_CHIP_ID_LOW_BYTE_ADDR  0x300B / VALUE 0x40
 *   OV5640_RST_ACTIVE 0 / INACTIVE 1      (RSTB 低有效)
 *   OV5640_PWDN_ACTIVE 1 / INACTIVE 0     (PWDN 高有效)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 MCLK + 复位时序并读 PID。返回 0 = 认出 OV5640。
 *  失败返回: -1 I2C 总线/设备注册失败 · -2 上电时序失败 · -3 SCCB 读失败
 *            · -4 PID 不是 0x56/0x40 */
int camera_probe(void);

/** `test cam` 注册项 */
int camera_self_test(void);

/** 控制台 `cam [addr_hex]` —— 换地址重试探测, 不用重启。
 *  不传地址 = 用默认 0x3C。用于现场区分"地址不对"与"模块没应答"。 */
void camera_cmd(int addr);

#ifdef __cplusplus
}
#endif
