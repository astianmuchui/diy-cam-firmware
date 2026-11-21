#pragma once

#include <Arduino.h>
#include "network.h"

void camera_init(Network *net);

bool camera_ping();

bool camera_ping_retry(uint8_t attempts, uint32_t delay_ms);

bool camera_trigger(const char *reading_id);

bool camera_wait_and_forward(const char *reading_id, uint32_t timeout_ms);
