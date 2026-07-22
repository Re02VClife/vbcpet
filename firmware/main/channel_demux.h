/** 双通道分流器 — 按行前缀分离 FRAME / CTRL / 原始文本
 *
 * 从 transport 读取字节流，识别：
 *   FRAME:<hex_size>\n + <binary JPEG> → frame_handler
 *   CTRL:<json>\n                       → ctrl_handler
 *   其他任意行                           → raw_line_handler
 *
 * 完全同步，不依赖 FreeRTOS。poll() 无数据时立即返回。
 */
#pragma once
#include "transport.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** JPEG 帧回调: 解码后的二进制数据 + 长度 */
typedef void (*frame_handler_t)(const uint8_t *data, size_t len, void *ctx);

/** JSON 控制消息回调: 去掉 "CTRL:" 前缀后的纯 JSON 字符串 */
typedef void (*ctrl_handler_t)(const char *json, void *ctx);

/** 原始文本行回调: 不匹配任何前缀的行 */
typedef void (*raw_line_handler_t)(const char *line, void *ctx);

/** 初始化分流器，绑定传输层 */
void channel_demux_init(transport_t *transport);

/** 设置帧处理器 */
void channel_demux_set_frame_handler(frame_handler_t handler, void *ctx);

/** 设置控制消息处理器 */
void channel_demux_set_ctrl_handler(ctrl_handler_t handler, void *ctx);

/** 设置原始行处理器 (用户输入等) */
void channel_demux_set_raw_handler(raw_line_handler_t handler, void *ctx);

/** 非阻塞轮询 — 从 transport 读可用字节，识别并分发消息。
 *  调用频率影响帧接收延迟，建议主循环中每 ~1ms 调用一次。 */
void channel_demux_poll(void);

#ifdef __cplusplus
}
#endif
