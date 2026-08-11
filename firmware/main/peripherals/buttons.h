/** 按键 ×4 (凯华热插拔轴座, 低有效) — 消抖 + 上报 PC button 事件 */
#pragma once
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化按键轮询任务 (10ms 消抖, 按下上报 vbc_build_button)。 */
int buttons_init(transport_t *transport);

/** 自检: 阻塞 5s 打印每个按键状态变化。返回 0=通过。 */
int buttons_self_test(void);

#ifdef __cplusplus
}
#endif
