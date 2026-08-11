/** USB-A 外设口 (FS OTG host, GPIO26/27, peripheral_map=BIT1)
 *
 * 用途: U 盘/鼠标/CDC 设备/机械臂 SO101 控制板 (CDC-ACM, 协议待确认)。
 * 本阶段只做枚举检测 (VID/PID); CDC-ACM 数据通路板到后补。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 安装 USB host (FS OTG)。返回 0 成功。 */
int usb_host_periph_init(void);

/** 自检: 安装 host + 事件循环 15s, 打印枚举到的设备 (请插入设备)。返回 0=通过。 */
int usb_host_periph_self_test(void);

#ifdef __cplusplus
}
#endif
