/** MIPI DSI 显示后端 — 最终板 (鱼鹰 ESP32-P4C6-Core + YDP430BT009-V1)
 *
 * 屏: 4.3" 480×800 ST7102 MIPI-DSI 2-lane + I2C 触摸 (触摸驱动见 peripherals/touch.c)
 * 驱动: 屏厂 osptek 官方组件 (vendored components/esp_lcd_st7102)
 *   来源: gitee.com/osptek/4.3-tft-480x800-mipi-st7102
 *         versions/YDP430BT009-V1/examples/esp32p4-idf5_st7102-mipi_esp-lvgl-port_lvgl9
 *   (2026-09 换版: 原 kevincoooool/esp_lcd_st7102 与 osptek 组件同源, 仅版权头/API 适配差异;
 *    换版意义在于对齐屏厂官方示例的 3 处关键配置 + 屏厂专用 init 序列)
 * 引脚:
 *   LCD_RST = GPIO20 → 屏 RSTB
 *   BL_PWM  = GPIO48 → MT3608 EN (背光 PWM 调光)
 * 帧缓冲: RGB565 16bpp, 480×800 = 768KB, PSRAM (display_pipeline.c 分配)
 * 刷屏: 单缓冲解码→文字叠加→draw_bitmap 全屏推入 DPI 面板内部 FB
 *
 * ★ 换 osptek 官方驱动后对齐的 3 处实质差异 (旧实现 → 屏厂示例):
 *   1. DSI PHY 供电: 新增 esp_ldo VO3 2.5V (VDD_MIPI_DPHY)。
 *      旧固件完全没开 → PHY 无电 → esp_lcd_new_dsi_bus 里 PLL 等锁 busy-wait 卡死
 *      (即 WORKLOG 记录的 "DSI HOST busy-wait" 症状, 屏点不亮的第一嫌疑根因)。
 *   2. lane_bit_rate_mbps: 500 → 820 (屏厂示例值)。
 *   3. PHY PLL 参考时钟: RC_FAST → XTAL 40M。
 *      旧代码注释写"显式用 XTAL 40MHz"但实际选错常量 RC_FAST (内部 RC ~17.5MHz, 高漂移,
 *      不适合做 DPHY PLL 基准); XTAL 也是 IDF 5.5.3 对 rev≥v1.3 的官方 DEFAULT。
 * init 序列: 换为屏厂 GX09C_ST7102+BOE4.3_2LANE 序列 (= osptek with-te 示例同款),
 *   含本屏 BOE 4.3" 面板专用 VGH/VGL(0xB0)/Source(0xB7)/VCOM(0xBF)/Gamma(0xC8/C9)/
 *   GIP(0xD7,A3,A6,A7,AC,AD) 参数; 0x11 退 sleep 延时 600ms (旧默认只有 120ms)。
 */
#include "display_backend.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7102.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "disp-dsi";

/* ── 引脚 ── */
#define PIN_LCD_RST   GPIO_NUM_20
#define PIN_BL_PWM    GPIO_NUM_48
#define BL_BRIGHTNESS (10 * 1023 / 10)   /* 默认背光 100% (bring-up 先看全亮) */

/* ── DSI PHY 供电 (osptek 官方示例: new_dsi_bus 前必调) ── */
#define DSI_PHY_LDO_CHAN        3     /* LDO_VO3 → VDD_MIPI_DPHY */
#define DSI_PHY_LDO_VOLTAGE_MV  2500

/* ── 静态句柄 ── */
static esp_lcd_dsi_bus_handle_t    g_bus   = NULL;
static esp_lcd_panel_io_handle_t   g_io    = NULL;
static esp_lcd_panel_handle_t      g_panel = NULL;
static bool                        g_bl_ready  = false;  /* 背光 LEDC 是否已初始化 */
static bool                        g_force_init = false; /* 'lcd' 命令强制点屏 (绕过 skip) */
static bool                        g_phy_pwr   = false;  /* PHY LDO 是否已使能 */
static esp_ldo_channel_handle_t    g_ldo_phy   = NULL;

/* ── YDP430BT009-V1 屏厂 init 序列 ──
 * 来源: _osptek/versions/YDP430BT009-V1/docs/GX09C_ST7102+BOE4.3_2LANE_90HZ.c
 *       (= osptek with-te 示例 components/hw_init/lcd/lcd_init_mipi_dsi.c 内嵌序列)
 */
static const st7102_lcd_init_cmd_t ydp430_vendor_init_cmds[] = {
//  {cmd, { data }, data_size, delay_ms}
    {0x99, (uint8_t []){0x71,0x02,0xa2}, 3, 0},   /* 命令区解锁×3 */
    {0x99, (uint8_t []){0x71,0x02,0xa3}, 3, 0},
    {0x99, (uint8_t []){0x71,0x02,0xa4}, 3, 0},
    {0xB0, (uint8_t []){0x22,0x57,0x1E,0x61,0x2F,0x57,0x61}, 7, 0},  /* VGH/VGL (14V) */
    {0xB7, (uint8_t []){0x64,0x64}, 2, 0},                           /* Source (5V) */
    {0xBF, (uint8_t []){0xB4,0xB4}, 2, 0},                           /* VCOM (-2V) */
    {0xC8, (uint8_t []){0x00,0x00,0x13,0x24,0x44,0x00,0x74,0x03,0xB8,0x04,
                        0x11,0x16,0x08,0x86,0x04,0x21,0xD3,0x02,0x10,0x0F,
                        0x22,0x4D,0x0E,0x90,0x09,0x32,0xF0,0x0B,0x40,0x0E,
                        0xF3,0x7D,0x0E,0xA9,0xBF,0x03,0xC4}, 37, 0}, /* Gamma P (5.5V) */
    {0xC9, (uint8_t []){0x00,0x00,0x13,0x24,0x44,0x00,0x74,0x03,0xB8,0x04,
                        0x11,0x16,0x08,0x86,0x04,0x21,0xD3,0x02,0x10,0x0F,
                        0x22,0x4D,0x0E,0x90,0x09,0x32,0xF0,0x0B,0x40,0x0E,
                        0xF3,0x7D,0x0E,0xA9,0xBF,0x03,0xC4}, 37, 0}, /* Gamma N (5.5V) */
    {0xD7, (uint8_t []){0x10,0x0C,0x36,0x19,0x90,0x90}, 6, 0},       /* GIP (800 行) */
    {0xA3, (uint8_t []){0x51,0x03,0x80,0xCF,0x44,0x00,0x00,0x00,0x00,0x04,
                        0x78,0x78,0x00,0x1A,0x00,0x45,0x05,0x00,0x00,0x00,
                        0x00,0x46,0x00,0x00,0x02,0x20,0x52,0x00,0x05,0x00,
                        0x00,0xFF}, 32, 0},
    {0xA6, (uint8_t []){0x02,0x00,0x24,0x55,0x35,0x00,0x38,0x00,0x78,0x78,
                        0x00,0x24,0x55,0x36,0x00,0x37,0x00,0x78,0x78,0x02,
                        0xAC,0x51,0x3A,0x00,0x00,0x00,0x78,0x78,0x03,0xAC,
                        0x21,0x00,0x04,0x00,0x00,0x78,0x78,0x3e,0x00,0x06,
                        0x00,0x00,0x00,0x00}, 44, 0},
    {0xA7, (uint8_t []){0x19,0x19,0x00,0x64,0x40,0x07,0x16,0x40,0x00,0x04,
                        0x03,0x78,0x78,0x00,0x64,0x40,0x25,0x34,0x00,0x00,
                        0x02,0x01,0x78,0x78,0x00,0x64,0x40,0x4B,0x5A,0x00,
                        0x00,0x02,0x01,0x78,0x78,0x00,0x24,0x40,0x69,0x78,
                        0x00,0x00,0x00,0x00,0x78,0x78,0x00,0x44}, 48, 0},
    {0xAC, (uint8_t []){0x08,0x0A,0x11,0x00,0x13,0x03,0x1B,0x18,0x06,0x1A,
                        0x19,0x1B,0x1B,0x1B,0x18,0x1B,0x09,0x0B,0x10,0x02,
                        0x12,0x01,0x1B,0x18,0x06,0x1A,0x19,0x1B,0x1B,0x1B,
                        0x18,0x1B,0xFF,0x67,0xFF,0x67,0x00}, 37, 0},
    {0xAD, (uint8_t []){0xCC,0x40,0x46,0x11,0x04,0x78,0x78}, 7, 0},
    {0xE8, (uint8_t []){0x30,0x07,0x00,0x94,0x94,0x9C,0x00,0xE2,0x04,0x00,
                        0x00,0x00,0x00,0xEF}, 14, 0},
    {0xE7, (uint8_t []){0x8B,0x3C,0x00,0x0C,0xF0,0x5D,0x00,0x5D,0x00,0x5D,
                        0x00,0x5D,0x00,0xFF,0x00,0x08,0x7B,0x00,0x00,0xC8,
                        0x6A,0x5A,0x08,0x1A,0x3C,0x00,0x81,0x01,0xCC,0x01,
                        0x7F,0xF0,0x22}, 33, 0},
    {0x11, (uint8_t []){0x00}, 0, 600},   /* Sleep Out — 屏厂要求 600ms (旧 120ms) */
    {0x29, (uint8_t []){0x00}, 0, 120},   /* Display On */
    {0x35, (uint8_t []){0x00}, 1, 0},     /* Tearing Effect ON (GX09C 序列尾部) */
};

/* ── 背光: LEDC PWM → MT3608 EN ── */
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_BL_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = BL_BRIGHTNESS,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    g_bl_ready = true;
    ESP_LOGI(TAG, "Backlight PWM GPIO%d duty=%d/1023", PIN_BL_PWM, BL_BRIGHTNESS);
}

/** 背光亮度 0-100 (供 bring-up 命令调节) */
void display_backend_set_brightness(int percent)
{
    if (!g_bl_ready) { ESP_LOGW(TAG, "backlight not initialized (skip display mode?)"); return; }
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (percent * 1023) / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* DSI PHY 2.5V 供电 — 只做一次 (osptek 官方示例在 esp_lcd_new_dsi_bus 之前必调;
 * 缺此步 PHY 无电, PLL 永不锁定 → new_dsi_bus busy-wait 卡死) */
static void dsi_phy_power_on(void)
{
    if (g_phy_pwr) return;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = DSI_PHY_LDO_CHAN,
        .voltage_mv = DSI_PHY_LDO_VOLTAGE_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &g_ldo_phy);
    g_phy_pwr = (err == ESP_OK);
    ESP_LOGI(TAG, "DSI PHY power LDO_VO%d %dmV: %s(err=%d)",
             DSI_PHY_LDO_CHAN, DSI_PHY_LDO_VOLTAGE_MV, err == ESP_OK ? "OK" : "FAIL", err);
}

/* 面板早期硬件复位 — 必须在首个 DSI 事务(组件构造函数读 ID)之前做:
 * 否则 LCD_RST(GPIO20) 自上电浮空, 屏躺在 reset 状态, DBI 读 BTA 无应答 → 忙等卡死
 * (实测卡在 mipi_dsi_hal_host_gen_read_short_packet, A4=0x500a0000)。
 * 时序按屏厂 GX09C: RESET 低 10ms → 高 → 等 600ms 再发命令。 */
static void panel_hw_reset_early(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_RST,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(600));
    ESP_LOGI(TAG, "Panel HW reset done (RST low 10ms -> high, wait 600ms)");
}

/* 真实 DSI 初始化 (始终编译; SKIP 时由 'lcd' 命令强制调用) */
static int dsi_backend_start(void)
{
    ESP_LOGI(TAG, "Initializing MIPI DSI backend (%dx%d) [osptek driver]...", LCD_W, LCD_H);

    backlight_init();

    /* -1. 面板早期复位 (先于一切 DSI 事务) */
    panel_hw_reset_early();

    /* 0. DSI PHY 供电 (VDD_MIPI_DPHY 2.5V) — 必须在建总线之前 */
    dsi_phy_power_on();

    /* 1. MIPI-DSI 总线 (2 lane, 820Mbps, PLL_F20M 基准)
     * 注: P4 (rev<3.0) 的 DPHY PLL 参考时钟 mux 仅支持 PLL_F20M/RC_FAST/PLL_F25M,
     *     不支持 XTAL (选之 abort, mipi_dsi_ll.h:272 实测崩溃); PLL_F20M 为 IDF 官方
     *     DEFAULT (v1.3 芯片), 之前 "F20M 锁不定" 实为 DSI PHY 未供电 (LDO 已补)。 */
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = 2,
        .phy_clk_src        = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M,
        .lane_bit_rate_mbps = 820,
    };
    esp_err_t __e = esp_lcd_new_dsi_bus(&bus_cfg, &g_bus);
    ESP_LOGI(TAG, "new_dsi_bus(bus=%d lanes=%d rate=%.1f src=%d[6=PLL_F20M]) ret=%d(0x%X)",
             bus_cfg.bus_id, bus_cfg.num_data_lanes, bus_cfg.lane_bit_rate_mbps, (int)bus_cfg.phy_clk_src, __e, __e);
    if (__e != ESP_OK) { ESP_LOGE(TAG, "DSI bus init FAIL, no abort"); return -1; }

    /* 2. DBI IO (命令通道) */
    ESP_LOGI(TAG, "step: creating DBI panel io...");
    esp_lcd_dbi_io_config_t io_cfg = ST7102_MIPI_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(g_bus, &io_cfg, &g_io), "io", "DSI DBI IO fail");

    /* 3. DPI 面板 (视频数据通道, 480×800) */
    ESP_LOGI(TAG, "step: creating ST7102 panel (ID read skipped in bring-up)...");
    esp_lcd_dpi_panel_config_t dpi_cfg = ST7102_MIPI_480_800_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_FMT_RGB565);
    st7102_vendor_config_t vendor_cfg = {
        .init_cmds      = ydp430_vendor_init_cmds,   /* 屏厂 GX09C 序列 (替代组件默认) */
        .init_cmds_size = sizeof(ydp430_vendor_init_cmds) / sizeof(ydp430_vendor_init_cmds[0]),
        .mipi_config = {
            .dsi_bus     = g_bus,
            .dpi_config  = &dpi_cfg,
        },
        .flags = { .use_mipi_interface = 1 },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,           /* RGB565, colmod 0x55 */
        .vendor_config  = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7102(g_io, &panel_cfg, &g_panel), "panel", "ST7102 panel create fail");

    /* 4. 复位 + 初始化 (屏厂 init 序列 + DPI 启动) */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), "panel", "reset fail");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), "panel", "init fail");

    /* 5. 显式 Display On (双保险; osptek 官方示例在 init 后调用) */
    esp_lcd_panel_disp_on_off(g_panel, true);

    ESP_LOGI(TAG, "MIPI DSI backend ready (osptek driver / 820Mbps / XTAL ref / vendor init seq)");
    return 0;
}

int display_backend_init(void)
{
#ifdef CONFIG_VBCPET_SKIP_DISPLAY
    if (!g_force_init) {
        /* bring-up: 默认跳过 DSI (屏未接/DSI 未调通会卡死在 esp_lcd_panel_init)。
         * 返回 0 但 g_panel 保持 NULL — 上层 (display_pipeline/text_render) 均已做 NULL 容错。 */
        ESP_LOGW(TAG, "Display backend SKIPPED (SKIP_DISPLAY=y) — panel=NULL; 'lcd' 强制点屏");
        return 0;
    }
    ESP_LOGW(TAG, "lcd: FORCED DSI init (bypass SKIP_DISPLAY)");
#endif
    return dsi_backend_start();
}

void display_backend_deinit(void)
{
    if (g_panel) { esp_lcd_panel_del(g_panel); g_panel = NULL; }
    if (g_io)    { esp_lcd_panel_io_del(g_io); g_io = NULL; }
    if (g_bus)   { esp_lcd_del_dsi_bus(g_bus); g_bus = NULL; }
}

void display_backend_flush(const uint16_t *fb)
{
    if (g_panel) {
        esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_W, LCD_H, fb);
    }
}

esp_lcd_panel_handle_t display_backend_get_panel(void)
{
    return g_panel;
}

/** 请求强制初始化 (由 bring-up 'lcd' 命令调用, 串口触发才点屏) */
void display_backend_request_init(void)
{
    g_force_init = true;
}

/** 独立背光使能 (不碰 DSI): 先确保 LEDC 初始化, 再设亮度百分比 */
void display_backend_enable_backlight(int percent)
{
    if (!g_bl_ready) backlight_init();
    display_backend_set_brightness(percent);
}
