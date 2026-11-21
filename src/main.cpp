#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <time.h>

#include "defines.h"
#include "imu.h"
#include "network.h"
#include "buzzer.h"
#include "camera.h"
#include "led.h"

static const BaseType_t CPU0 = 0;
static const BaseType_t CPU1 = 1;

#ifdef USE_GSM
static Network g_net(Transport::GSM);
#else
static Network g_net(Transport::WIFI);
#endif

typedef struct
{
    double lat;
    double lon;
    bool gps_valid;

    float ultrasonic_cm;
    bool ultrasonic_valid;
    float depth_baseline_m;

    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;

    bool photo_triggered;
    char photo_reading_id[40];
    char photo_upload_status[16];
} sensor_snapshot_t;

static sensor_snapshot_t g_snapshot = {};
static SemaphoreHandle_t g_data_mutex;
static SemaphoreHandle_t g_publish_sem;
static SemaphoreHandle_t g_camera_sem;

static void on_publish_timer(TimerHandle_t )
{
    xSemaphoreGive(g_publish_sem);
}

static void publish_task(void *)
{
    sensor_snapshot_t snap;
    char buf[768];

    while (true)
    {

        bool should_publish =
            (xSemaphoreTake(g_publish_sem, pdMS_TO_TICKS(5000)) == pdTRUE);

        if (g_net.is_net_connected())
            g_net.loop();

        if (!should_publish)
            continue;

        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        {
            Serial.println("[Publish] Mutex timeout — skipping");
            continue;
        }
        snap = g_snapshot;
        xSemaphoreGive(g_data_mutex);

        JsonDocument doc;
        doc["device_id"] = DEVICE_ID;
        doc["timestamp"] = (long long)time(nullptr);

        JsonObject gps = doc["gps"].to<JsonObject>();
        gps["lat"] = snap.lat;
        gps["lon"] = snap.lon;
        gps["accuracy"] = 0.0f;

        JsonObject ultra = doc["ultrasonic"].to<JsonObject>();
        ultra["distance_cm"] = snap.ultrasonic_cm;
        ultra["valid"] = snap.ultrasonic_valid;

        JsonObject mpu = doc["mpu6050"].to<JsonObject>();
        mpu["accel_x"] = snap.accel_x / ACCEL_SCALE;
        mpu["accel_y"] = snap.accel_y / ACCEL_SCALE;
        mpu["accel_z"] = snap.accel_z / ACCEL_SCALE;
        mpu["gyro_x"] = snap.gyro_x / GYRO_SCALE;
        mpu["gyro_y"] = snap.gyro_y / GYRO_SCALE;
        mpu["gyro_z"] = snap.gyro_z / GYRO_SCALE;

        JsonObject sonar = doc["sonar"].to<JsonObject>();
        sonar["echo_amplitude"] = 0.0f;
        sonar["fish_detected"] = false;
        sonar["fish_count"] = 0;
        sonar["fish_type"] = "";

        JsonObject sys = doc["system"].to<JsonObject>();
        sys["battery_voltage"] = 0.0f;
        sys["signal_strength"] = g_net.signal_strength();
        sys["transmission_type"] = g_net.transport_name();

        JsonObject cam = doc["camera"].to<JsonObject>();
        cam["photo_triggered"] = snap.photo_triggered;
        cam["reading_id"] = snap.photo_reading_id;
        cam["upload_status"] = snap.photo_upload_status;

        size_t len = serializeJson(doc, buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf))
        {
            Serial.println("[Publish] JSON overflow");
            continue;
        }

        bool ok = g_net.publish(TOPIC_READINGS, buf);
        led_set(ok ? LedEvent::PUBLISH_OK : LedEvent::PUBLISH_FAIL);
        Serial.printf("[Publish] readings → %s (%s)\n",
                      ok ? "OK" : "FAIL", buf);

        g_net.publish(TOPIC_HEARTBEAT, "{}");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

static void network_init_task(void *)
{
    Serial.println("[Net] Starting SoftAP first...");
    if (g_net.start_softap(SOFTAP_SSID, SOFTAP_PASSWORD))
        led_set(LedEvent::HOTSPOT_ON);
    vTaskDelay(pdMS_TO_TICKS(500));

    camera_init(&g_net);

#ifdef USE_GSM
    bool radio_ok = g_net.begin(GSM_APN);
#else
    bool radio_ok = g_net.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
    if (!radio_ok)
    {
        Serial.println("[Net] Radio init failed");
        buzzer_tone_error();
        led_set(LedEvent::ERROR);
    }

    Serial.println("[Cam] Pinging ESP-CAM...");
    led_set(LedEvent::CAM_CHECKING);
    bool cam_up = camera_ping_retry(8, 1500);
    led_set(cam_up ? LedEvent::CAM_ONLINE : LedEvent::CAM_OFFLINE);
    Serial.printf("[Cam] Ping %s\n", cam_up ? "OK — ESP-CAM online" : "failed — ESP-CAM offline");
    vTaskDelay(pdMS_TO_TICKS(700));

    const char *user = (MQTT_USER[0]) ? MQTT_USER : nullptr;
    const char *pass = (MQTT_PASS[0]) ? MQTT_PASS : nullptr;
    g_net.connect_mqtt(MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID, user, pass);

    TimerHandle_t timer = xTimerCreate(
        "pub_timer",
        pdMS_TO_TICKS(PUBLISH_INTERVAL_MS),
        pdTRUE,
        nullptr,
        on_publish_timer);
    if (timer)
        xTimerStart(timer, 0);
    else
        Serial.println("[Timer] Failed to create publish timer!");

    Serial.printf("[Net] Publish timer started — interval %d s\n",
                  PUBLISH_INTERVAL_MS / 1000);

    vTaskDelete(nullptr);
}

static float read_ultrasonic()
{
    digitalWrite(HC_SR04_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(HC_SR04_TRIG, LOW);
    long dur = pulseIn(HC_SR04_ECHO, HIGH, 24000);
    if (dur == 0)
        return -1.0f;
    float dist = (dur * 0.0343f) / 2.0f;
    if (dist < 2.0f || dist > 400.0f)
        return -1.0f;
    return dist;
}

static float    s_depth_history[ROLLING_BASELINE_N] = {};
static int      s_depth_count = 0;
static int      s_depth_idx   = 0;
static uint32_t s_last_trigger_ms = 0;
#define TRIGGER_COOLDOWN_MS 30000

static void ultrasonic_task(void *)
{
    while (true)
    {
        float dist = read_ultrasonic();
        bool  valid = dist > 0.0f;

        if (valid)
        {
            float depth_m = dist / 100.0f;

            s_depth_history[s_depth_idx] = depth_m;
            s_depth_idx = (s_depth_idx + 1) % ROLLING_BASELINE_N;
            if (s_depth_count < ROLLING_BASELINE_N)
                s_depth_count++;

            float baseline = 0.0f;
            for (int i = 0; i < s_depth_count; i++)
                baseline += s_depth_history[i];
            baseline /= s_depth_count;

            float deviation = fabsf(depth_m - baseline);
            bool do_trigger = (s_depth_count >= ROLLING_BASELINE_N) &&
                              (deviation > PHOTO_TRIGGER_THRESHOLD_M) &&
                              (millis() - s_last_trigger_ms > TRIGGER_COOLDOWN_MS);

            xSemaphoreTake(g_data_mutex, portMAX_DELAY);
            g_snapshot.ultrasonic_cm      = dist;
            g_snapshot.ultrasonic_valid   = true;
            g_snapshot.depth_baseline_m   = baseline;

            if (do_trigger && !g_snapshot.photo_triggered)
            {
                g_snapshot.photo_triggered = true;
                uint32_t r1 = esp_random(), r2 = esp_random(),
                         r3 = esp_random(), r4 = esp_random();
                snprintf(g_snapshot.photo_reading_id,
                         sizeof(g_snapshot.photo_reading_id),
                         "%08lx-%04lx-%04lx-%04lx-%04lx%08lx",
                         (unsigned long)r1,
                         (unsigned long)(r2 >> 16),
                         (unsigned long)((r2 & 0x0FFF) | 0x4000),
                         (unsigned long)((r3 >> 16 & 0x3FFF) | 0x8000),
                         (unsigned long)(r3 & 0xFFFF),
                         (unsigned long)r4);
                strlcpy(g_snapshot.photo_upload_status, "pending",
                        sizeof(g_snapshot.photo_upload_status));
                s_last_trigger_ms = millis();
                xSemaphoreGive(g_data_mutex);

                Serial.printf("[US] Deviation %.2f m — triggering photo\n", deviation);
                xSemaphoreGive(g_camera_sem);
            }
            else
            {
                xSemaphoreGive(g_data_mutex);
            }
        }
        else
        {
            xSemaphoreTake(g_data_mutex, portMAX_DELAY);
            g_snapshot.ultrasonic_valid = false;
            xSemaphoreGive(g_data_mutex);
        }

        Serial.printf("[US] %.1f cm%s\n", dist, valid ? "" : " (invalid)");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

static void imu_snapshot_task(void *)
{
    while (true)
    {
        accel_data_t a = imu_get_accel();
        gyro_data_t g = imu_get_gyro();

        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        g_snapshot.accel_x = a.x;
        g_snapshot.accel_y = a.y;
        g_snapshot.accel_z = a.z;
        g_snapshot.gyro_x = g.x;
        g_snapshot.gyro_y = g.y;
        g_snapshot.gyro_z = g.z;
        xSemaphoreGive(g_data_mutex);

        Serial.printf("[IMU] A(%.0f,%.0f,%.0f) G(%.0f,%.0f,%.0f)\n",
                      a.x, a.y, a.z, g.x, g.y, g.z);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void gps_task(void *)
{
    bool s_gps_fixed = false;
    while (true)
    {
        gps_data_t d = READ_GPS();

        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        g_snapshot.gps_valid = d.valid;
        if (d.valid)
        {
            g_snapshot.lat = d.latitude;
            g_snapshot.lon = d.longitude;
        }
        xSemaphoreGive(g_data_mutex);

        if (d.valid)
        {
            if (!s_gps_fixed)
            {
                s_gps_fixed = true;
                buzzer_tone_gps_fix();
                led_set(LedEvent::GPS_FIX);
            }
            Serial.printf("[GPS] %.6f, %.6f  sats=%u\n",
                          d.latitude, d.longitude, d.satellites);
        }
        else
        {
            Serial.println("[GPS] Waiting for fix...");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void periodic_photo_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(PHOTO_PERIODIC_INTERVAL_MS));

    while (true)
    {
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        bool busy = g_snapshot.photo_triggered;
        if (!busy)
        {
            g_snapshot.photo_triggered = true;
            uint32_t r1 = esp_random(), r2 = esp_random(),
                     r3 = esp_random(), r4 = esp_random();
            snprintf(g_snapshot.photo_reading_id,
                     sizeof(g_snapshot.photo_reading_id),
                     "%08lx-%04lx-%04lx-%04lx-%04lx%08lx",
                     (unsigned long)r1,
                     (unsigned long)(r2 >> 16),
                     (unsigned long)((r2 & 0x0FFF) | 0x4000),
                     (unsigned long)((r3 >> 16 & 0x3FFF) | 0x8000),
                     (unsigned long)(r3 & 0xFFFF),
                     (unsigned long)r4);
            strlcpy(g_snapshot.photo_upload_status, "pending",
                    sizeof(g_snapshot.photo_upload_status));
        }
        xSemaphoreGive(g_data_mutex);

        if (!busy)
        {
            Serial.println("[Cam] Periodic 10 s trigger");
            xSemaphoreGive(g_camera_sem);
        }

        vTaskDelay(pdMS_TO_TICKS(PHOTO_PERIODIC_INTERVAL_MS));
    }
}

static void camera_task(void *)
{
    while (true)
    {
        if (xSemaphoreTake(g_camera_sem, portMAX_DELAY) != pdTRUE)
            continue;

        char reading_id[40];
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        strlcpy(reading_id, g_snapshot.photo_reading_id, sizeof(reading_id));
        xSemaphoreGive(g_data_mutex);

        Serial.printf("[Cam] Photo trigger — reading %s\n", reading_id);
        buzzer_tone_detection();
        led_set(LedEvent::FISH_DETECTED);

        bool triggered = camera_trigger(reading_id);
        const char *status = "fail";

        if (triggered)
        {
            bool forwarded = camera_wait_and_forward(reading_id, 20000);
            status = forwarded ? "success" : "fail";
        }

        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        strlcpy(g_snapshot.photo_upload_status, status,
                sizeof(g_snapshot.photo_upload_status));
        g_snapshot.photo_triggered = false;
        xSemaphoreGive(g_data_mutex);

        Serial.printf("[Cam] Done — status: %s\n", status);
    }
}

void setup()
{
    Serial.begin(115200);

    led_init();
    pinMode(HC_SR04_TRIG, OUTPUT);
    pinMode(HC_SR04_ECHO, INPUT);

    g_data_mutex  = xSemaphoreCreateMutex();
    g_publish_sem = xSemaphoreCreateBinary();
    g_camera_sem  = xSemaphoreCreateBinary();

    buzzer_init();
    buzzer_tone_boot();
    GPS_INIT();

    if (!imu_init())
        Serial.println("[IMU] Init failed — IMU task will not start");
    else
        xTaskCreatePinnedToCore(imu_snapshot_task, "IMU", 4096, nullptr, 1, nullptr, CPU0);

    xTaskCreatePinnedToCore(led_task, "LED", 2048, nullptr, 1, nullptr, CPU0);
    xTaskCreatePinnedToCore(ultrasonic_task, "US", 2048, nullptr, 1, nullptr, CPU0);
    xTaskCreatePinnedToCore(gps_task, "GPS", 4096, nullptr, 1, nullptr, CPU0);

    xTaskCreatePinnedToCore(publish_task,         "Pub",     12288, nullptr, 2, nullptr, CPU1);
    xTaskCreatePinnedToCore(camera_task,          "Cam",     12288, nullptr, 2, nullptr, CPU1);
    xTaskCreatePinnedToCore(periodic_photo_task,  "CamTmr",  3072,  nullptr, 1, nullptr, CPU1);
    xTaskCreatePinnedToCore(network_init_task,    "NetInit", 16384, nullptr, 3, nullptr, CPU1);
}

void loop()
{

    vTaskDelay(pdMS_TO_TICKS(1000));
}
