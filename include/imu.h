#ifndef IMU_H
#define IMU_H

#include <stdbool.h>

typedef struct gyro_data_s
{
    float x;
    float y;
    float z;
} gyro_data_t;

typedef struct accel_data_s
{
    float x;
    float y;
    float z;
} accel_data_t;

typedef struct dht_data_s
{
    float temperature;
    float humidity;
} dht_data_t;

bool         imu_init(void);
accel_data_t imu_get_accel(void);
gyro_data_t  imu_get_gyro(void);
void         imu_task(void *ptr);

#endif
