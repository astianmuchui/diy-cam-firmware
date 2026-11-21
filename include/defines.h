#ifndef DEFINES_H
#define DEFINES_H

#include <stdint.h>

#define RED_PIN 5
#define GREEN_PIN 18
#define BLUE_PIN 19

#define HC_SR04_TRIG 27
#define HC_SR04_ECHO 13

#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600

#define I2C_SDA 21
#define I2C_SCL 22
#define MPU6500_ADDR 0x68

#define ACCEL_SCALE 16384.0f
#define GYRO_SCALE 131.0f

#define GSM_RX_PIN 32
#define GSM_TX_PIN 33
#define GSM_BAUD 9600
#define GSM_APN "safaricom"

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""

#define DEVICE_ID "IRK17352YV2026"
#define MQTT_CLIENT_ID "IRK17352YV2026"

#define TOPIC_READINGS "sonar/devices/" DEVICE_ID "/readings"
#define TOPIC_HEARTBEAT "sonar/devices/" DEVICE_ID "/heartbeat"

#define PUBLISH_INTERVAL_MS 30000

#define BUZZER_PIN 25

#define PHOTO_TRIGGER_THRESHOLD_M 0.3f
#define ROLLING_BASELINE_N        10

#define PHOTO_PERIODIC_INTERVAL_MS 10000

#define SOFTAP_SSID     "MSONAR-" DEVICE_ID
#define SOFTAP_PASSWORD "CHANGE_ME_PASSWORD"

#define ESPCAM_IP "192.168.4.2"

#define GO_BACKEND_HOST "your-backend-host.example.com"
#define GO_BACKEND_PORT 443
#define GO_BACKEND_PATH "/api/v1/photos/upload"

typedef struct gps_data_s
{
    double latitude;
    double longitude;
    double altitude;
    double speed;
    uint32_t satellites;
    bool valid;
} gps_data_t;

void GPS_INIT();
gps_data_t READ_GPS();
void GPS_TASK(void *ptr);

#endif
