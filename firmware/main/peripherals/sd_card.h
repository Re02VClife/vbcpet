/** microSD (SDIO 4-bit, GPIO39-44 + CD GPIO45) — FATFS 挂载到 /sd */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 挂载 SD 卡到 "/sd"。返回 0 成功。 */
int sd_card_init(void);

/** 自检: 挂载→列根目录→写读 1 个测试文件→卸载。返回 0=通过。 */
int sd_card_self_test(void);

#ifdef __cplusplus
}
#endif
