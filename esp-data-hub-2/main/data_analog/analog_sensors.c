#include "analog_sensors.h"

#include <stdbool.h>

#include "analog_sensors_backend.h"
#include "pressure_filter.h"
#include "sdkconfig.h"

static const analog_sensor_backend_t *s_backend = NULL;
static bool s_initialized = false;
static pressure_filter_t s_pressure_filter;

esp_err_t analog_sensors_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

#if CONFIG_DH_ANALOG_USE_MOCK
  s_backend = analog_sensors_mock_backend();
#else
  s_backend = analog_sensors_real_backend();
#endif

  if (s_backend == NULL || s_backend->init == NULL || s_backend->read == NULL || s_backend->deinit == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = s_backend->init();
  if (err != ESP_OK) {
    return err;
  }

  const pressure_filter_config_t filter_config = {
      .sample_period_ms = CONFIG_DH_ANALOG_POLL_PERIOD_MS,
      .normal_time_constant_ms = CONFIG_DH_OIL_PRESSURE_FILTER_NORMAL_TAU_MS,
      .fast_time_constant_ms = CONFIG_DH_OIL_PRESSURE_FILTER_FAST_TAU_MS,
      .fast_step_psi = CONFIG_DH_OIL_PRESSURE_FILTER_FAST_STEP_PSI,
      .fast_hold_ms = CONFIG_DH_OIL_PRESSURE_FILTER_FAST_HOLD_MS,
      .immediate_low_psi = CONFIG_DH_OIL_PRESSURE_FILTER_IMMEDIATE_LOW_PSI,
  };
  if (!pressure_filter_init(&s_pressure_filter, &filter_config)) {
    s_backend->deinit();
    return ESP_ERR_INVALID_ARG;
  }

  s_initialized = true;
  return ESP_OK;
}

esp_err_t analog_sensors_read(analog_sensor_reading_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized || s_backend == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  analog_sensor_reading_t reading = {0};
  esp_err_t err = s_backend->read(&reading);
  if (err != ESP_OK) {
    return err;
  }

  reading.oil_pressure_filtered_psi =
      pressure_filter_apply(&s_pressure_filter, reading.oil_pressure_raw_psi);
  *out = reading;
  return ESP_OK;
}

void analog_sensors_deinit(void) {
  if (!s_initialized || s_backend == NULL) {
    return;
  }

  s_backend->deinit();
  s_initialized = false;
}
