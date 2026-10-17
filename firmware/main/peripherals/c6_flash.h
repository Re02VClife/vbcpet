/** P4 当 C6 的串口编程器 (V1 飞线 / V2 板级方案通用)
 *
 * 背景: 模组 ESP32-P4C6-Core 里的 C6 只引出了 UART(pin4/5) 和 BOOT strap(pin9),
 * 没有 EN。C6 是空片时 ROM 会停在 UART 下载模式; 若 C6 里是坏固件, 则需要
 * "IO9 拉低 + 断电重上电" 才进下载。P4 经 GPIO30/31 与 C6 的 UART 相连,
 * 用官方 esp-serial-flasher 直接把镜像写进 C6。
 *
 * 用法 (控制台):
 *   c6flash            默认: 带 stub + 460800, 等 60s
 *   c6flash rom        不带 stub (纯 ROM 协议, 最稳但慢)
 *   c6flash 921600 120 指定波特率/等待秒数
 *   c6mon [秒数] [波特率] 被动监听 C6 的 UART (hex+ASCII, 看它有没有在启动/打印日志)
 *   c6scan             扫候选脚找 C6 的 EN/唤醒脚 (GPIO54/53/52/0/49/50 × 高/低/高阻)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** 烧录 C6 (阻塞)。返回 0 成功。use_stub=false 时走纯 ROM 协议 */
int c6_flash_run(bool use_stub, uint32_t baud, int wait_seconds);
/** 被动监听 C6 UART, 打印原始字节 (hex+ASCII 分栏) */
void c6_flash_monitor(int seconds);
/** 同上, 但可指定波特率 (扫别的波特率看 C6 日志是否可读) */
void c6_flash_monitor_baud(int seconds, uint32_t baud);
/** 控制台入口: c6flash / c6mon */
void c6_flash_cmd(const char *sub, const char *rest);
/** 开机钩子: 若 CONFIG_VBCPET_C6_FLASH_ON_BOOT=y 则在 5s 后自动烧 C6 (不依赖控制台) */
void c6_flash_boot_hook(void);
