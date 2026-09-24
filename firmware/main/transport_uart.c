/** UART0 传输实现 — Live2D 推流复用链路 (console 让位: CONFIG_ESP_CONSOLE_NONE)
 *
 * V1 板 USJ (GPIO24/25) 被舵机 PWM 占用 (final-pinmap.md:141), 推流改走 CH340 UART。
 * 波特率 CONFIG_VBCPET_UART_BAUDRATE (默认 2M, CH340 上限 2Mbps)。
 * 帧协议不变: FRAME:hexsize\n + JPEG (channel_demux 解析)。
 */
#include "transport.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

#ifndef CONFIG_VBCPET_UART_BAUDRATE
#define CONFIG_VBCPET_UART_BAUDRATE 2000000
#endif

static const char *TAG = "tr-uart";

typedef struct {
    bool installed;
} uart_ctx_t;

static int uart_tr_send(transport_t *t, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    int total = 0;
    while (total < (int)len) {
        int n = uart_write_bytes(UART_NUM_0, (const char *)(data + total), len - total);
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }  /* TX 环满 → 短等重试 */
        total += n;
    }
    return total;
}

static int uart_tr_recv(transport_t *t, uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    (void)t;
    return uart_read_bytes(UART_NUM_0, buf, max_len, pdMS_TO_TICKS(timeout_ms));
}

static void uart_tr_flush(transport_t *t)
{
    (void)t;
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
}

static void uart_tr_close(transport_t *t)
{
    uart_ctx_t *ctx = (uart_ctx_t *)t->ctx;
    if (ctx && ctx->installed) {
        uart_driver_delete(UART_NUM_0);
        ctx->installed = false;
    }
    free(ctx);
    free(t);
}

transport_t *transport_uart_create(int rx_buf_size, int tx_buf_size)
{
    (void)tx_buf_size;
    uart_config_t cfg = {
        .baud_rate  = CONFIG_VBCPET_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_XTAL,   /* 40M XTAL: 2M 精确分频, 不受 APB 影响 */
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
    /* 不调 uart_set_pin → 使用 IOMUX 默认脚 (与原 console 相同, CH340 接线不变) */
    esp_err_t ret = uart_driver_install(UART_NUM_0,
                                        rx_buf_size > 0 ? rx_buf_size : 65536,
                                        0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %d", ret);
        return NULL;
    }

    transport_t *t = (transport_t *)calloc(1, sizeof(transport_t));
    uart_ctx_t *ctx = (uart_ctx_t *)calloc(1, sizeof(uart_ctx_t));
    if (!t || !ctx) { free(t); free(ctx); uart_driver_delete(UART_NUM_0); return NULL; }
    ctx->installed = true;

    t->ctx   = ctx;
    t->send  = uart_tr_send;
    t->recv  = uart_tr_recv;
    t->flush = uart_tr_flush;
    t->close = uart_tr_close;

    ESP_LOGI(TAG, "UART0 transport ready (baud=%d rx=%d)", CONFIG_VBCPET_UART_BAUDRATE, rx_buf_size);
    return t;
}
