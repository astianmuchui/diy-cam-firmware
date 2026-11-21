#include "led.h"
#include "defines.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static volatile LedEvent s_state = LedEvent::IDLE;

static void rgb(uint8_t r, uint8_t g, uint8_t b)
{
    analogWrite(RED_PIN, r);
    analogWrite(GREEN_PIN, g);
    analogWrite(BLUE_PIN, b);
}

void led_init()
{
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    rgb(0, 0, 0);
}

void led_set(LedEvent event)
{
    s_state = event;
}

void led_task(void *)
{
    int16_t breath = 0;
    int8_t  dir    = 3;

    while (true)
    {
        switch (s_state)
        {
        case LedEvent::IDLE:
            rgb(0, 0, (uint8_t)breath);
            breath += dir;
            if (breath >= 240 || breath <= 0) dir = -dir;
            vTaskDelay(pdMS_TO_TICKS(15));
            break;

        case LedEvent::WIFI_CONNECTING:
            rgb(0, 0, 200);
            vTaskDelay(pdMS_TO_TICKS(150));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
            break;

        case LedEvent::WIFI_CONNECTED:
            rgb(0, 220, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::BROKER_CONNECTED:
            rgb(0, 180, 180);
            vTaskDelay(pdMS_TO_TICKS(500));
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::HOTSPOT_ON:
            rgb(0, 0, 200);
            vTaskDelay(pdMS_TO_TICKS(200));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            rgb(0, 0, 200);
            vTaskDelay(pdMS_TO_TICKS(200));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::PUBLISH_OK:
            rgb(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
            rgb(0, 0, 0);
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::PUBLISH_FAIL:
            rgb(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
            rgb(0, 0, 0);
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::GPS_FIX:
            rgb(220, 220, 220);
            vTaskDelay(pdMS_TO_TICKS(500));
            rgb(0, 0, 0);
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::FISH_DETECTED:
            rgb(200, 0, 200);
            vTaskDelay(pdMS_TO_TICKS(800));
            rgb(0, 0, 0);
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::CAM_CHECKING:
            rgb(200, 80, 0);
            vTaskDelay(pdMS_TO_TICKS(120));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(120));
            break;

        case LedEvent::CAM_ONLINE:
            rgb(220, 100, 0);
            vTaskDelay(pdMS_TO_TICKS(600));
            rgb(0, 0, 0);
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::CAM_OFFLINE:
            rgb(200, 80, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(80));
            rgb(200, 80, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(80));
            s_state = LedEvent::IDLE;
            break;

        case LedEvent::ERROR:
            rgb(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
    }
}
