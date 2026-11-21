#include <Arduino.h>

#include "imu.h"
#include "defines.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MPU6500_REG_PWR_MGMT_1   0x6B
#define MPU6500_REG_ACCEL_XOUT_H 0x3B
#define MPU6500_REG_GYRO_XOUT_H  0x43

bool imu_init(void)
{
    Wire.begin(I2C_SDA, I2C_SCL);

    Wire.beginTransmission(MPU6500_ADDR);
    Wire.write(MPU6500_REG_PWR_MGMT_1);
    Wire.write(0);
    Wire.endTransmission(true);

    Serial.println("[IMU] MPU6500 initialized.");
    return true;
}

accel_data_t imu_get_accel(void)
{
    accel_data_t accel;

    Wire.beginTransmission(MPU6500_ADDR);
    Wire.write(MPU6500_REG_ACCEL_XOUT_H);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6500_ADDR, (size_t)6, true);

    accel.x = (int16_t)(Wire.read() << 8 | Wire.read());
    accel.y = (int16_t)(Wire.read() << 8 | Wire.read());
    accel.z = (int16_t)(Wire.read() << 8 | Wire.read());

    return accel;
}

gyro_data_t imu_get_gyro(void)
{
    gyro_data_t gyro;

    Wire.beginTransmission(MPU6500_ADDR);
    Wire.write(MPU6500_REG_GYRO_XOUT_H);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6500_ADDR, (size_t)6, true);

    gyro.x = (int16_t)(Wire.read() << 8 | Wire.read());
    gyro.y = (int16_t)(Wire.read() << 8 | Wire.read());
    gyro.z = (int16_t)(Wire.read() << 8 | Wire.read());

    return gyro;
}

void imu_task(void *ptr)
{
    while (1)
    {
        accel_data_t accel = imu_get_accel();
        gyro_data_t  gyro  = imu_get_gyro();

        Serial.print("Accel X: "); Serial.print(accel.x);
        Serial.print(" | Accel Y: "); Serial.print(accel.y);
        Serial.print(" | Accel Z: "); Serial.print(accel.z);
        Serial.print(" | Gyro X: "); Serial.print(gyro.x);
        Serial.print(" | Gyro Y: "); Serial.print(gyro.y);
        Serial.print(" | Gyro Z: "); Serial.println(gyro.z);

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
