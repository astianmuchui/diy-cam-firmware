#include <Arduino.h>
#include "defines.h"
#include <TinyGPS++.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

void GPS_INIT()
{
    Serial.println("Initializing GPS .......");
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
}

gps_data_t READ_GPS()
{
    while (gpsSerial.available() > 0)
        gps.encode(gpsSerial.read());

    gps_data_t data = {};
    data.valid = gps.location.isValid();
    data.latitude = gps.location.lat();
    data.longitude = gps.location.lng();
    data.altitude = gps.altitude.meters();
    data.speed = gps.speed.kmph();
    data.satellites = gps.satellites.value();
    return data;
}

void GPS_TASK(void *ptr)
{
    while (1)
    {
        gps_data_t data = READ_GPS();

        if (data.valid)
        {
            Serial.printf("[GPS] Lat: %.6f  Lon: %.6f  Alt: %.1f m  Speed: %.1f km/h  Sats: %u\n",
                          data.latitude, data.longitude, data.altitude,
                          data.speed, data.satellites);
        }
        else
        {
            Serial.println("[GPS] Waiting for fix...");
            Serial.printf("[GPS] {INVALID}  Lat: %.6f  Lon: %.6f  Alt: %.1f m  Speed: %.1f km/h  Sats: %u\n",
                          data.latitude, data.longitude, data.altitude,
                          data.speed, data.satellites);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
