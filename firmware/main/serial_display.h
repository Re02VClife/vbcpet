/** USB 串口 JPEG 帧接收 + ST7789V 显示 — vbcpet Phase 0
 *
 * 协议 (PC → P4):
 *   LINE: "FRAME:<hex_size>"  (8 hex chars, e.g. FRAME:00003a1f)
 *   DATA: <jpeg_bytes>
 *
 * 依赖: ESP-IDF esp_driver_jpeg (硬件 PPA JPEG 解码)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动串口接收任务。调用后阻塞式循环接收帧并显示。 */
int serial_display_start(void);

/** 停止接收任务，释放资源。 */
void serial_display_stop(void);

#ifdef __cplusplus
}
#endif
