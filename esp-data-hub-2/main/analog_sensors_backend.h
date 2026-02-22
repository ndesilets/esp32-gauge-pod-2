#pragma once

#include "analog_sensors.h"
#include "esp_err.h"

typedef struct {
  esp_err_t (*init)(void);
  esp_err_t (*read)(analog_sensor_reading_t *out);
  void (*deinit)(void);
} analog_sensor_backend_t;

const analog_sensor_backend_t *analog_sensors_real_backend(void);
const analog_sensor_backend_t *analog_sensors_mock_backend(void);
