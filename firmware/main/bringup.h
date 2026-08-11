/** Bring-up 框架 — 外设初始化 + UART0 控制台命令 (仅最终板) */
#pragma once
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化全部外设 (幂等, 逐个容错)。 */
int bringup_init(transport_t *transport);

/** 启动 UART0 控制台任务 (在 bringup_init 之后调用)。 */
void bringup_start_console(void);

#ifdef __cplusplus
}
#endif
