/** USB Serial JTAG 传输实现 */
#include "transport.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "tr-usb";

typedef struct {
    bool installed;
} usb_ctx_t;

static int usb_send(transport_t *t, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    int total = 0;
    while (total < (int)len) {
        int n = usb_serial_jtag_write_bytes(data + total, len - total,
                                             pdMS_TO_TICKS(timeout_ms));
        if (n <= 0) break;
        total += n;
    }
    return total;
}

static int usb_recv(transport_t *t, uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    return usb_serial_jtag_read_bytes(buf, max_len, pdMS_TO_TICKS(timeout_ms));
}

static void usb_flush(transport_t *t)
{
    (void)t;
}

static void usb_close(transport_t *t)
{
    usb_ctx_t *ctx = (usb_ctx_t *)t->ctx;
    if (ctx && ctx->installed) {
        usb_serial_jtag_driver_uninstall();
        ctx->installed = false;
    }
    free(ctx);
    free(t);
}

transport_t *transport_usb_create(int rx_buf_size, int tx_buf_size)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = rx_buf_size > 0 ? rx_buf_size : 16384;
    cfg.tx_buffer_size = tx_buf_size > 0 ? tx_buf_size : 4096;
    esp_err_t ret = usb_serial_jtag_driver_install(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB Serial JTAG driver install failed: %d", ret);
        return NULL;
    }

    transport_t *t = (transport_t *)calloc(1, sizeof(transport_t));
    usb_ctx_t *ctx = (usb_ctx_t *)calloc(1, sizeof(usb_ctx_t));
    if (!t || !ctx) {
        free(t);
        free(ctx);
        usb_serial_jtag_driver_uninstall();
        return NULL;
    }
    ctx->installed = true;

    t->ctx   = ctx;
    t->send  = usb_send;
    t->recv  = usb_recv;
    t->flush = usb_flush;
    t->close = usb_close;

    ESP_LOGI(TAG, "USB Serial JTAG ready (rx=%d tx=%d)", rx_buf_size, tx_buf_size);
    return t;
}

void transport_destroy(transport_t *t)
{
    if (t) t->close(t);
}
