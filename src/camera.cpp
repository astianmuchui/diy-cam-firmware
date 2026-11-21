#include "camera.h"
#include "defines.h"

#include <Arduino.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ESPAsyncWebServer.h>

static Network *s_net = nullptr;
static SemaphoreHandle_t s_upload_sem = nullptr;

static uint8_t *s_jpeg_buf = nullptr;
static size_t   s_jpeg_len = 0;
static char     s_upload_reading_id[40] = {};

#define MAX_PHOTO_BYTES (64 * 1024)

static AsyncWebServer s_srv(80);

static void on_upload_body(AsyncWebServerRequest *req,
                            uint8_t *data, size_t len,
                            size_t index, size_t total)
{
    if (index == 0)
    {
        if (s_jpeg_buf)
        {
            free(s_jpeg_buf);
            s_jpeg_buf = nullptr;
        }
        if (total == 0 || total > MAX_PHOTO_BYTES)
        {
            Serial.printf("[Cam] Bad upload size: %zu\n", total);
            return;
        }
        s_jpeg_buf = (uint8_t *)malloc(total);
        if (!s_jpeg_buf)
        {
            Serial.println("[Cam] OOM for photo buffer");
            return;
        }
        s_jpeg_len = total;
        if (req->hasParam("id"))
            strlcpy(s_upload_reading_id,
                    req->getParam("id")->value().c_str(),
                    sizeof(s_upload_reading_id));
    }

    if (s_jpeg_buf)
        memcpy(s_jpeg_buf + index, data, len);

    if (index + len == total && s_jpeg_buf && s_upload_sem)
        xSemaphoreGive(s_upload_sem);
}

void camera_init(Network *net)
{
    s_net = net;
    s_upload_sem = xSemaphoreCreateBinary();

    s_srv.on(
        "/upload", HTTP_POST,
        [](AsyncWebServerRequest *req)
        {
            req->send(200, "application/json", "{\"status\":\"received\"}");
        },
        nullptr,
        on_upload_body);

    s_srv.begin();
    Serial.println("[Cam] Photo proxy server started on :80");
}

bool camera_ping()
{
    WiFiClient client;
    bool ok = client.connect(ESPCAM_IP, 80, 2000);
    if (ok)
        client.stop();
    return ok;
}

bool camera_ping_retry(uint8_t attempts, uint32_t delay_ms)
{
    for (uint8_t i = 0; i < attempts; i++)
    {
        if (camera_ping())
        {
            Serial.printf("[Cam] Ping OK on attempt %u/%u\n", i + 1, attempts);
            return true;
        }
        Serial.printf("[Cam] Ping attempt %u/%u failed — retrying in %lu ms\n",
                      i + 1, attempts, (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return false;
}

bool camera_trigger(const char *reading_id)
{
    WiFiClient client;
    if (!client.connect(ESPCAM_IP, 80))
    {
        Serial.println("[Cam] Cannot reach ESP-CAM at " ESPCAM_IP);
        return false;
    }

    char body[72];
    snprintf(body, sizeof(body), "{\"reading_id\":\"%s\"}", reading_id);
    int blen = (int)strlen(body);

    client.printf(
        "POST /capture HTTP/1.1\r\n"
        "Host: " ESPCAM_IP "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        blen, body);

    uint32_t t = millis();
    while (!client.available() && millis() - t < 5000)
        vTaskDelay(pdMS_TO_TICKS(100));

    String resp = client.readStringUntil('\n');
    client.stop();
    Serial.printf("[Cam] Trigger → %s\n", resp.c_str());
    return resp.indexOf("200") != -1;
}

bool camera_wait_and_forward(const char *reading_id, uint32_t timeout_ms)
{
    if (!s_upload_sem)
    {
        Serial.println("[Cam] Upload sem not ready — camera_init not called yet");
        return false;
    }
    if (xSemaphoreTake(s_upload_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        Serial.println("[Cam] Upload timeout — no photo from ESP-CAM");
        return false;
    }
    if (!s_jpeg_buf || s_jpeg_len == 0)
    {
        Serial.println("[Cam] Upload sem fired but buffer empty");
        return false;
    }

    bool ok = s_net->http_post_jpeg(
        GO_BACKEND_HOST, GO_BACKEND_PORT, GO_BACKEND_PATH,
        s_jpeg_buf, s_jpeg_len, reading_id);

    free(s_jpeg_buf);
    s_jpeg_buf = nullptr;
    s_jpeg_len = 0;
    return ok;
}
