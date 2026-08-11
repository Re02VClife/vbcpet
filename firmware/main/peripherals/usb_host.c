/** USB-A 外设口 FS OTG host 实现 — 枚举检测最小版 */
#include "usb_host.h"
#include "esp_log.h"
#include "esp_check.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "usbh";

static bool s_installed = false;
static usb_host_client_handle_t s_client = NULL;

/* 同步客户端回调 (在 usb_host_client_handle_events 上下文中执行) */
static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    usb_host_client_handle_t client = s_client;
    (void)arg;
    switch (msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV: {
        usb_device_handle_t dev = NULL;
        if (usb_host_device_open(client, msg->new_dev.address, &dev) == ESP_OK) {
            usb_device_info_t info;
            const usb_device_desc_t *desc = NULL;
            if (usb_host_device_info(dev, &info) == ESP_OK &&
                usb_host_get_device_descriptor(dev, &desc) == ESP_OK) {
                ESP_LOGI(TAG, "NEW_DEV addr=%d VID=0x%04X PID=0x%04X speed=%s",
                         msg->new_dev.address, desc->idVendor, desc->idProduct,
                         info.speed == USB_SPEED_LOW ? "LOW" :
                         info.speed == USB_SPEED_FULL ? "FULL" : "HS");
            }
            usb_host_device_close(client, dev);
        }
        break;
    }
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGI(TAG, "DEV_GONE");
        break;
    }
}

int usb_host_periph_init(void)
{
    if (s_installed) return 0;

    /* P4 双 OTG: FS 外设 = 控制器 index 1 (GPIO26/27), 用 peripheral_map=BIT1 */
    usb_host_config_t host_cfg = {
        .skip_phy_setup    = false,
        .intr_flags        = ESP_INTR_FLAG_LEVEL1,
        .peripheral_map    = BIT1,
    };
    esp_err_t ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install fail: %d", ret);
        return -1;
    }
    s_installed = true;

    ESP_LOGI(TAG, "USB host installed (FS OTG, peripheral_map=BIT1)");
    return 0;
}

int usb_host_periph_self_test(void)
{
    if (usb_host_periph_init() != 0) return -1;

    usb_host_client_config_t client_cfg = {
        .is_synchronous = true,
        .max_num_event_msg = 8,
        .async = { .client_event_callback = client_event_cb, .callback_arg = NULL },
    };
    if (usb_host_client_register(&client_cfg, &s_client) != ESP_OK) {
        ESP_LOGE(TAG, "client register fail");
        return -2;
    }

    ESP_LOGI(TAG, "Self-test: 15s 内插入 USB 设备 (U盘/鼠标/机械臂)…");
    uint32_t event_flags = 0;
    int64_t deadline = xTaskGetTickCount() * portTICK_PERIOD_MS + 15000;
    while (xTaskGetTickCount() * portTICK_PERIOD_MS < deadline) {
        usb_host_lib_handle_events(100, &event_flags);
        usb_host_client_handle_events(s_client, 100);   /* 触发回调 */
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    usb_host_client_deregister(s_client);
    s_client = NULL;
    return 0;
}
