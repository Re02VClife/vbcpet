/** 交互引擎 — 系统调度核心 ("大脑")
 *
 * 职责:
 *   1. 接收 CTRL: JSON 控制消息，分发处理
 *   2. 接收用户原始文本输入，构建 user_text 回传 PC
 *   3. 周期性心跳发送
 *   4. 开机握手 (hello)
 *
 * 依赖: transport (发送), protocol (构建/解析), display_pipeline (直接绘图),
 *       text_render (文字显示), text_input (文字输入)。
 */
#pragma once
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

int engine_init(transport_t *transport);
void engine_on_ctrl_message(const char *json);
void engine_on_raw_line(const char *line);
void engine_poll(void);
void engine_send_hello(void);

/** 获取待显示的文字 (调用者 free)，无待显示时返回 NULL */
char *engine_get_pending_text(void);

#ifdef __cplusplus
}
#endif
