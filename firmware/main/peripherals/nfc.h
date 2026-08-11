/** NFC NT3H2111 (I2C 0x55, FD GPIO11) — 手机触碰检测 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 (共享 I2C + FD GPIO 输入)。返回 0 成功。 */
int nfc_init(void);

/** 读取用户内存 offset 处 len 字节。 */
int nfc_read_user(uint8_t offset, uint8_t *buf, size_t len);

/** 当前是否处于 NFC 场 (FD 引脚)。 */
bool nfc_field_present(void);

/** 自检: 探测 + 读用户内存 + 打印 FD。返回 0=通过。 */
int nfc_self_test(void);

#ifdef __cplusplus
}
#endif
