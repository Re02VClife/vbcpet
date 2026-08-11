/** MPU-6500 驱动实现 */
#include "mpu6500.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "mpu";

#define MPU6500_WHO_AM_I       0x75
#define MPU6500_PWR_MGMT_1     0x6B
#define MPU6500_ACCEL_CONFIG   0x1C
#define MPU6500_GYRO_CONFIG    0x1B
#define MPU6500_INT_PIN_CFG    0x37
#define MPU6500_INT_ENABLE     0x38
#define MPU6500_ACCEL_XOUT_H   0x3B
#define MPU6500_GYRO_XOUT_H    0x43
#define WHO_AM_I_VAL           0x70

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50);
}

static esp_err_t read_bytes(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

int mpu6500_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c bus init fail");
    ESP_RETURN_ON_ERROR(i2c_bus_add_device(MPU6500_I2C_ADDR, 400000, &s_dev), TAG, "i2c add device fail");

    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(reg_read(MPU6500_WHO_AM_I, &who), TAG, "who_am_i read fail");
    if (who != WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "WHO_AM_I = 0x%02X (期望 0x70) — 可能是 MPU-6050 或未焊好", who);
    }

    /* 唤醒 + 关闭睡眠 */
    ESP_RETURN_ON_ERROR(reg_write(MPU6500_PWR_MGMT_1, 0x00), TAG, "wake fail");
    ESP_RETURN_ON_ERROR(reg_write(MPU6500_ACCEL_CONFIG, 0x00), TAG, "accel cfg fail");  /* ±2g */
    ESP_RETURN_ON_ERROR(reg_write(MPU6500_GYRO_CONFIG,  0x00), TAG, "gyro cfg fail");   /* ±250dps */

    ESP_LOGI(TAG, "MPU-6500 ready (I2C 0x%02X, INT GPIO%d)", MPU6500_I2C_ADDR, PIN_MPU_INT);
    return 0;
}

void mpu6500_read_raw(int16_t *accel_xyz, int16_t *gyro_xyz)
{
    if (!s_dev) return;
    uint8_t buf[14];
    if (read_bytes(MPU6500_ACCEL_XOUT_H, buf, sizeof(buf)) != ESP_OK) return;

    for (int i = 0; i < 3; i++) {
        accel_xyz[i] = (int16_t)((buf[i*2] << 8) | buf[i*2+1]);
        gyro_xyz[i]  = (int16_t)((buf[8+i*2] << 8) | buf[8+i*2+1]);
    }
}

int mpu6500_self_test(void)
{
    uint8_t who = 0;
    if (reg_read(MPU6500_WHO_AM_I, &who) != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read FAIL");
        return -1;
    }
    if (who != WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "WHO_AM_I = 0x%02X (期望 0x70) — 可能不是 MPU-6500", who);
        return -2;
    }

    int16_t acc[3], gyr[3];
    for (int i = 0; i < 10; i++) {
        mpu6500_read_raw(acc, gyr);
        ESP_LOGI(TAG, "  acc=%6d %6d %6d  gyro=%6d %6d %6d",
                 acc[0], acc[1], acc[2], gyr[0], gyr[1], gyr[2]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return 0;
}
