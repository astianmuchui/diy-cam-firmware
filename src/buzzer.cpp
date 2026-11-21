#include "buzzer.h"
#include "defines.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define BUZZER_CH  4
#define BUZZER_RES 8

struct BuzNote { uint16_t freq; uint16_t ms; };

static QueueHandle_t s_q;

static void buzzer_task(void *)
{
    BuzNote n;
    while (true)
    {
        if (xQueueReceive(s_q, &n, portMAX_DELAY))
        {
            ledcWriteTone(BUZZER_CH, n.freq);
            vTaskDelay(pdMS_TO_TICKS(n.ms));
            ledcWriteTone(BUZZER_CH, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

void buzzer_init()
{
    ledcSetup(BUZZER_CH, 2700, BUZZER_RES);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWriteTone(BUZZER_CH, 0);
    s_q = xQueueCreate(8, sizeof(BuzNote));
    xTaskCreate(buzzer_task, "Buz", 1024, nullptr, 1, nullptr);
}

void buzzer_beep(int freq_hz, int duration_ms)
{
    BuzNote n = {(uint16_t)freq_hz, (uint16_t)duration_ms};
    xQueueSend(s_q, &n, 0);
}

void buzzer_tone_detection()
{
    buzzer_beep(700, 40);
    buzzer_beep(900, 60);
}

void buzzer_tone_error()
{
    buzzer_beep(500, 120);
}

void buzzer_tone_gps_fix()
{
    buzzer_beep(900, 50);
}

void buzzer_tone_boot()
{
    buzzer_beep(600, 60);
    buzzer_beep(800, 60);
    buzzer_beep(1000, 80);
}
