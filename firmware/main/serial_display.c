/** USB 串口 JPEG 帧接收 + ST7789V 显示 — vbcpet Phase 0

 * 协议 (PC → P4):
 *   LINE: "FRAME:<hex_size>"  (8 hex chars, e.g. FRAME:00003a1f)
 *   DATA: <jpeg_bytes>

 * 使用 ESP32-P4 硬件 PPA JPEG 解码器，直接输出 RGB565 到 ST7789V。
 */
#include "serial_display.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "driver/jpeg_decode.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

static const char *TAG = "serial_disp";

// ── 引脚 (与 display_test.c 相同) ──
#define PIN_CS     GPIO_NUM_16
#define PIN_DC     GPIO_NUM_17
#define PIN_RST    GPIO_NUM_18
#define PIN_MOSI   GPIO_NUM_19
#define PIN_SCLK   GPIO_NUM_20

#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// ── 帧协议 ──
#define FRAME_PREFIX    "FRAME:"
#define MAX_JPEG_SIZE   (64 * 1024)   // 单帧 JPEG 不超过 64KB

// ── 全局句柄 ──
static esp_lcd_panel_handle_t  g_panel  = NULL;
static esp_lcd_panel_io_handle_t g_io   = NULL;
static jpeg_decoder_handle_t   g_jpeg   = NULL;
static uint8_t *g_dec_outbuf = NULL;       // 预分配解码输出缓冲
static size_t  g_dec_outbuf_size = 0;

// ── SPI 显示初始化 ──
static int display_init(void)
{
    ESP_LOGI(TAG, "初始化 SPI ST7789V...");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = PIN_CS,
        .dc_gpio_num       = PIN_DC,
        .spi_mode          = 0,
        .pclk_hz           = 10 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .cs_ena_pretrans   = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &g_io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(g_io, &panel_cfg, &g_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));

    // 方向 (关屏写)
    esp_lcd_panel_disp_on_off(g_panel, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(g_io, 0x36, (uint8_t[]){0x60}, 1);  // MX=MV=1
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_disp_on_off(g_panel, true);

    ESP_LOGI(TAG, "ST7789V 就绪");
    return 0;
}

// ── JPEG 解码器初始化 ──
static int jpeg_decoder_init(void)
{
    jpeg_decode_engine_cfg_t eng_cfg = {
        .intr_priority = 0,     // 默认优先级
        .timeout_ms    = 2000,  // 单帧超时 2s
    };
    esp_err_t ret = jpeg_new_decoder_engine(&eng_cfg, &g_jpeg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG 解码器创建失败: %d", ret);
        return -1;
    }
    ESP_LOGI(TAG, "JPEG 硬件解码器就绪");
    return 0;
}

// ── 解码 + 显示一帧 ──
static void decode_and_display(const uint8_t *jpeg_data, uint32_t jpeg_size)
{
    // 1) 获取图片尺寸
    jpeg_decode_picture_info_t pic_info;
    esp_err_t ret = jpeg_decoder_get_info(jpeg_data, jpeg_size, &pic_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG 头解析失败: %d", ret);
        return;
    }

    uint32_t w = pic_info.width;
    uint32_t h = pic_info.height;
    if (w == 0 || h == 0 || w > LCD_WIDTH || h > LCD_HEIGHT) {
        ESP_LOGW(TAG, "JPEG 尺寸异常: %lux%lu", w, h);
        return;
    }

    // 2) 使用预分配的输出缓冲
    uint8_t *outbuf = g_dec_outbuf;
    uint32_t outbuf_size = (uint32_t)g_dec_outbuf_size;
    if (!outbuf) {
        ESP_LOGE(TAG, "解码缓冲未分配");
        return;
    }

    // 3) 硬件解码
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t actual_out_size = 0;
    ret = jpeg_decoder_process(g_jpeg, &dec_cfg, jpeg_data, jpeg_size,
                               outbuf, outbuf_size, &actual_out_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG 解码失败: %d", ret);
        free(outbuf);
        return;
    }

    // 4) 推屏 (居中显示)
    int x_off = (LCD_WIDTH  - (int)w) / 2;
    int y_off = (LCD_HEIGHT - (int)h) / 2;
    if (x_off < 0) x_off = 0;
    if (y_off < 0) y_off = 0;

    // 先清屏为黑 (仅首帧)
    static bool first_frame = true;
    if (first_frame) {
        uint16_t *black = calloc(1, LCD_WIDTH * LCD_HEIGHT * 2);
        esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, black);
        free(black);
        first_frame = false;
    }

    esp_lcd_panel_draw_bitmap(g_panel, x_off, y_off, w, h, outbuf);

    // 调试: 输出首像素值到 USB Serial JTAG
    static int debug_count = 0;
    if (debug_count < 5) {
        uint16_t first_pixel = ((uint16_t*)outbuf)[0];
        // 检查像素分布
        uint32_t white_px = 0, black_px = 0, other_px = 0;
        uint32_t total = w * h;
        if (total > 10000) total = 10000;
        for (uint32_t i = 0; i < total; i++) {
            uint16_t px = ((uint16_t*)outbuf)[i];
            if (px == 0xFFFF) white_px++;
            else if (px == 0x0000) black_px++;
            else other_px++;
        }
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "DBG:%d sz=%lux%lu out=%lu 1st=0x%04X W=%lu B=%lu O=%lu\n",
                 debug_count, w, h, actual_out_size, first_pixel,
                 white_px, black_px, other_px);
        usb_serial_jtag_write_bytes(dbg, strlen(dbg), pdMS_TO_TICKS(100));
        debug_count++;
    }
}

// ── 串口行读取 (驱动层, ISR 负责从硬件 FIFO→ring buffer) ──
static bool read_line(char *buf, size_t max_len, uint32_t timeout_ms)
{
    size_t pos = 0;
    TickType_t start = xTaskGetTickCount();

    while (pos < max_len - 1) {
        uint8_t ch;
        int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(10));
        if (n == 1) {
            if (ch == '\n') {
                buf[pos] = '\0';
                if (pos > 0 && buf[pos - 1] == '\r') buf[pos - 1] = '\0';
                return true;
            }
            buf[pos++] = (char)ch;
            start = xTaskGetTickCount();
        }
        TickType_t elapsed_ticks = xTaskGetTickCount() - start;
        if (elapsed_ticks * portTICK_PERIOD_MS >= timeout_ms) {
            if (pos > 0) { buf[pos] = '\0'; return true; }
            return false;
        }
    }
    buf[pos] = '\0';
    return true;
}

// ── 从驱动 ring buffer 读取指定字节数 ──
static uint32_t read_bytes(uint8_t *dst, uint32_t want, uint32_t timeout_ms)
{
    uint32_t got = 0;
    TickType_t start = xTaskGetTickCount();
    while (got < want) {
        int n = usb_serial_jtag_read_bytes(dst + got, want - got, pdMS_TO_TICKS(100));
        if (n > 0) {
            got += n;
            start = xTaskGetTickCount();
        }
        TickType_t e = xTaskGetTickCount() - start;
        if (e * portTICK_PERIOD_MS >= timeout_ms) break;
    }
    return got;
}

// ── 公开 API ──
int serial_display_start(void)
{
    ESP_LOGI(TAG, "=== USB 串口 JPEG 显示模式 ===");

    // 1) 显示初始化
    if (display_init() != 0) {
        ESP_LOGE(TAG, "SPI 显示初始化失败");
        return -1;
    }

    // 2) JPEG 解码器
    if (jpeg_decoder_init() != 0) {
        return -1;
    }

    // 3) 帧缓冲
    uint8_t *jpeg_buf = malloc(MAX_JPEG_SIZE);
    if (!jpeg_buf) {
        ESP_LOGE(TAG, "帧缓冲 malloc 失败");
        return -1;
    }

    char line[256];
    uint32_t frame_count = 0;
    uint32_t total_jpeg_bytes = 0;

    ESP_LOGI(TAG, "等待 PC 端发送帧 (协议: FRAME:<hex_size>\\n + JPEG)...");
    ESP_LOGI(TAG, "串口波特率应设为 2M (idf.py monitor 缺省 115200 会乱码)");

    // 3) 预分配解码输出缓冲 (只分配一次，避免碎片)
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    g_dec_outbuf = jpeg_alloc_decoder_mem(LCD_WIDTH * LCD_HEIGHT * 2, &mem_cfg, &g_dec_outbuf_size);
    if (!g_dec_outbuf) {
        ESP_LOGE(TAG, "解码缓冲预分配失败! 尝试 heap_caps_malloc...");
        g_dec_outbuf = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        g_dec_outbuf_size = LCD_WIDTH * LCD_HEIGHT * 2;
    }
    ESP_LOGI(TAG, "解码缓冲: %p size=%zu %s", g_dec_outbuf, g_dec_outbuf_size,
             g_dec_outbuf ? "OK" : "FAIL");

    // 安装 USB Serial JTAG 驱动 (ISR 负责 RX)
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.rx_buffer_size = 16384;  // 加大接收缓冲
    esp_err_t usb_ret = usb_serial_jtag_driver_install(&usb_cfg);
    ESP_LOGI(TAG, "USB Serial JTAG 驱动: %s (%d)",
             usb_ret == ESP_OK ? "OK" : (usb_ret == ESP_ERR_INVALID_STATE ? "已安装" : "失败"), usb_ret);

    // 初始化 GPIO51 LED
    gpio_config_t led_conf = { .pin_bit_mask = BIT64(51), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&led_conf);
    gpio_set_level(GPIO_NUM_51, 1);  // LED on during init

    // 显示提示画面
    {
        uint16_t *wait_buf = malloc(LCD_WIDTH * LCD_HEIGHT * 2);
        if (wait_buf) {
            memset(wait_buf, 0x00, LCD_WIDTH * LCD_HEIGHT * 2);
            // 简单的灰色边框表示等待中
            for (int y = 0; y < LCD_HEIGHT; y++) {
                for (int x = 0; x < LCD_WIDTH; x++) {
                    if (x < 5 || x >= LCD_WIDTH - 5 || y < 5 || y >= LCD_HEIGHT - 5)
                        wait_buf[y * LCD_WIDTH + x] = 0x7E0;  // 绿色边框
                }
            }
            esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, wait_buf);
            free(wait_buf);
        }
    }

    // 4) 主循环: 读行 → 读 JPEG → 解码 → 显示
    while (1) {
        // 读取一行
        if (!read_line(line, sizeof(line), 5000)) {
            continue;
        }

        // 检查 FRAME 前缀
        if (strncmp(line, FRAME_PREFIX, strlen(FRAME_PREFIX)) != 0) {
            // 可能是日志行或其他数据，跳过
            // ESP_LOGI(TAG, "SKIP: %s", line);
            continue;
        }

        // 解析 hex 大小
        const char *hex = line + strlen(FRAME_PREFIX);
        uint32_t jpeg_size = 0;
        for (int i = 0; i < 8 && hex[i]; i++) {
            char c = hex[i];
            jpeg_size <<= 4;
            if (c >= '0' && c <= '9')      jpeg_size |= (c - '0');
            else if (c >= 'a' && c <= 'f') jpeg_size |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') jpeg_size |= (c - 'A' + 10);
            else break;
        }

        if (jpeg_size == 0 || jpeg_size > MAX_JPEG_SIZE) {
            ESP_LOGW(TAG, "非法帧大小: %lu (hex=%s)", jpeg_size, hex);
            continue;
        }

        // 读取 JPEG 数据 (从驱动 ring buffer)
        uint32_t received = read_bytes(jpeg_buf, jpeg_size, 2000);

        if (received != jpeg_size) {
            continue;  // 丢帧
        }

        // 解码 + 显示
        decode_and_display(jpeg_buf, jpeg_size);

        frame_count++;
        total_jpeg_bytes += jpeg_size;
        gpio_set_level(GPIO_NUM_51, frame_count & 1);  // LED 翻转证明帧被处理

        if (frame_count % 30 == 0) {
            ESP_LOGI(TAG, "帧 %lu: avg JPEG=%luB",
                     frame_count, total_jpeg_bytes / frame_count);
        }
    }

    free(jpeg_buf);
    return 0;
}

void serial_display_stop(void)
{
    if (g_jpeg) {
        jpeg_del_decoder_engine(g_jpeg);
        g_jpeg = NULL;
    }
    if (g_dec_outbuf) {
        free(g_dec_outbuf);
        g_dec_outbuf = NULL;
    }
    if (g_panel) {
        esp_lcd_panel_disp_on_off(g_panel, false);
        esp_lcd_panel_del(g_panel);
        g_panel = NULL;
    }
    if (g_io) {
        esp_lcd_panel_io_del(g_io);
        g_io = NULL;
    }
    spi_bus_free(SPI2_HOST);
    ESP_LOGI(TAG, "串口显示已关闭");
}
