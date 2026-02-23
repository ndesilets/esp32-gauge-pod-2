#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "monitoring.h"

typedef struct {
  monitored_state_t* state;
  SemaphoreHandle_t mutex;
} dd_state_iface_t;

esp_err_t dd_ui_controller_init(const dd_state_iface_t* shared_state);
void dd_ui_controller_render(const monitored_state_t* snapshot);
