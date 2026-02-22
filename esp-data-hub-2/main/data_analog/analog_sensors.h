#pragma once

#include "esp_err.h"

typedef struct {
  float oil_temp_f;
  float oil_pressure_psi;
} analog_sensor_reading_t;

esp_err_t analog_sensors_init(void);
esp_err_t analog_sensors_read(analog_sensor_reading_t *out);
void analog_sensors_deinit(void);
