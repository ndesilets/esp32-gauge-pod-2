#include "analog_sensors.h"

#include <stdbool.h>

#include "analog_sensors_backend.h"
#include "sdkconfig.h"

static const analog_sensor_backend_t *s_backend = NULL;
static bool s_initialized = false;

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

  return s_backend->read(out);
}

void analog_sensors_deinit(void) {
  if (!s_initialized || s_backend == NULL) {
    return;
  }

  s_backend->deinit();
  s_initialized = false;
}
