/** 文字输入 — 字符累积器
 *
 * 接收字符流或完整行，组装完成后回调 engine。
 * 不依赖任何硬件 — 纯数据模块。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 行就绪回调: line 以 '\0' 结尾，ctx 为注册时传入的上下文指针 */
typedef void (*text_line_callback_t)(const char *line, void *ctx);

/** 初始化，注册回调 */
void text_input_init(text_line_callback_t callback, void *ctx);

/** 喂入一行完整文本 (已含 '\0') */
void text_input_feed_line(const char *line);

#ifdef __cplusplus
}
#endif
