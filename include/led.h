#pragma once

enum class LedEvent
{
    IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    BROKER_CONNECTED,
    HOTSPOT_ON,
    PUBLISH_OK,
    PUBLISH_FAIL,
    GPS_FIX,
    FISH_DETECTED,
    CAM_CHECKING,
    CAM_ONLINE,
    CAM_OFFLINE,
    ERROR,
};

void led_init();
void led_set(LedEvent event);
void led_task(void *pvParams);
