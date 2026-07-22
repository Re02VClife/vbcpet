/** vbcpet 传输层抽象接口
 *
 * 封装物理层差异 (USB Serial JTAG / UART / SPI / WebRTC DataChannel)。
 * 上层代码只依赖 transport_t 接口，切换传输方式不影响业务逻辑。
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 传输层句柄 — 不透明，调用者只持有指针 */
typedef struct transport {
    void *ctx;
    int  (*send)(struct transport *t, const uint8_t *data, size_t len, uint32_t timeout_ms);
    int  (*recv)(struct transport *t, uint8_t *buf, size_t max_len, uint32_t timeout_ms);
    void (*flush)(struct transport *t);
    void (*close)(struct transport *t);
} transport_t;

/** 创建 USB Serial JTAG 传输实例 */
transport_t *transport_usb_create(int rx_buf_size, int tx_buf_size);

/** 释放传输实例及其资源 */
void transport_destroy(transport_t *t);

#ifdef __cplusplus
}
#endif
