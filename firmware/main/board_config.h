/** 最终板引脚配置 — 鱼鹰 ESP32-P4C6-Core 88pin (唯一权威来源)
 *
 * 依据 hardware/final-pinmap.md + hardware/expansion.md + 08-11 打样网表。
 * 调线(bring-up 改飞线/跳线)只改这一个文件。
 * 注意: 本文件只在最终板 (CONFIG_DISPLAY_BACKEND_DSI) 构建中使用;
 *       开发板 (WT9932P4-TINY) 走 SPI 后端, 不包含外设。
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 按键 ×4 (凯华热插拔轴座, K0→GPIO, K1→GND, 10k 上拉, 按下=LOW) ── */
#define PIN_BTN_YES           GPIO_NUM_1
#define PIN_BTN_NO            GPIO_NUM_2
#define PIN_BTN_YES_ALWAYS    GPIO_NUM_3
#define PIN_BTN_PANEL         GPIO_NUM_4

/* ── SK6812 ×4 (级联, GPIO51→33Ω→LED1_DIN, WS2812B 协议, 5V) ── */
#define PIN_SK6812_DIN        GPIO_NUM_51
#define SK6812_COUNT          4

/* ── I2C 总线 (共享: 触摸 / NFC / MPU-6500 / OV5640, 4.7k 上拉) ── */
#define PIN_I2C_SDA           GPIO_NUM_8
#define PIN_I2C_SCL           GPIO_NUM_21
#define I2C_FREQ_HZ           400000

/* ── 触摸 (ST7102 内置, 3.3V 直连) ── */
#define PIN_TP_RST            GPIO_NUM_9
#define PIN_TP_INT            GPIO_NUM_10
#define TOUCH_I2C_ADDR        0x24   /* ⚠️ 待 bring-up 探测确认 */

/* ── 音频 (I2S0 全双工, 无 MCLK) ── */
#define PIN_I2S_BCLK          GPIO_NUM_22
#define PIN_I2S_WS            GPIO_NUM_7
#define PIN_I2S_DOUT          GPIO_NUM_23   /* → MAX98357A DIN */
#define PIN_I2S_DIN           GPIO_NUM_6    /* ← ZTS6672×2 DATA (L/R 分声道) */
#define PIN_PA_EN             GPIO_NUM_46   /* MAX98357A SD_MODE, HIGH=开 */
#define AUDIO_SAMPLE_RATE     48000         /* 麦与功放共用 I2S 时钟 */

/* ── MPU-6500 六轴 (I2C 0x68, INT) ── */
#define MPU6500_I2C_ADDR      0x68
#define PIN_MPU_INT           GPIO_NUM_47

/* ── microSD (SDIO 4-bit) ── */
#define PIN_SD_DAT0           GPIO_NUM_39
#define PIN_SD_DAT1           GPIO_NUM_40
#define PIN_SD_DAT2           GPIO_NUM_41
#define PIN_SD_DAT3           GPIO_NUM_42
#define PIN_SD_CLK            GPIO_NUM_43
#define PIN_SD_CMD            GPIO_NUM_44
#define PIN_SD_CD             GPIO_NUM_45   /* 低=卡在位 (可选) */

/* ── 舵机 (50Hz, 500-2500µs, 5V) ── */
#define PIN_SERVO_YAW         GPIO_NUM_24
#define PIN_SERVO_PITCH       GPIO_NUM_25
#define PIN_SERVO3            GPIO_NUM_28   /* 预留 */
#define PIN_SERVO4            GPIO_NUM_29   /* 预留 */

/* ── NFC NT3H2111 (I2C 0x55, FD) ── */
#define NFC_I2C_ADDR          0x55
#define PIN_NFC_FD            GPIO_NUM_11

/* ── USB-A 外设口 (FS OTG, GPIO26/27 = 模组 pin53/54) ── */
#define PIN_USB_FS_DM         GPIO_NUM_26
#define PIN_USB_FS_DP         GPIO_NUM_27

/* ── 摄像头 OV5640 MIPI-CSI (esp_cam; 本阶段暂不启用) ── */
#define PIN_CAM_MCLK          GPIO_NUM_5
#define PIN_CAM_RSTB          GPIO_NUM_12
#define PIN_CAM_PWDN          GPIO_NUM_13

/* ── 显示 (DSI 后端已用, 供自检引用) ── */
#define PIN_LCD_RST           GPIO_NUM_20
#define PIN_BL_PWM            GPIO_NUM_48

/* ── 电源 (IP5306, 无 I2C) ── */
#define PIN_IP5306_KEY        GPIO_NUM_33   /* 可选软关机, 未接则悬空 */

#ifdef __cplusplus
}
#endif
