/** Bring-up 自检注册表 — 每个外设驱动注册 {name, run} */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 运行指定自检 (name 不区分大小写)。返回 0=通过, <0=未找到/失败。 */
int self_test_run(const char *name);

/** 运行全部自检, 打印汇总。 */
void self_test_run_all(void);

/** 列出所有可用自检。 */
void self_test_list(void);

#ifdef __cplusplus
}
#endif
