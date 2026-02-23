#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "monitoring.h"

esp_err_t dd_logger_init(monitored_state_t* state, SemaphoreHandle_t state_mutex);
esp_err_t dd_logger_start(void);
void dd_logger_stop(void);
bool dd_logger_is_active(void);
bool dd_logger_is_ready(void);
