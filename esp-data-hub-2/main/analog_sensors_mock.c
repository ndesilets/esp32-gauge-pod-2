#include "analog_sensors_backend.h"

#include <math.h>

#include "analog_sensors_math.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "analog_mock";

static float mock_step(void) {
  static float x = 0.0f;
  x += 0.001f;
  return x;
}

static float uniform_noise(float amplitude) {
  int32_t r = (int32_t)(esp_random() % 2001U) - 1000;
  return ((float)r / 1000.0f) * amplitude;
}

static esp_err_t mock_init(void) {
  analog_reset_temp_smoothing();
  ESP_LOGI(TAG, "Mock analog sensors enabled");
  return ESP_OK;
}

static esp_err_t mock_read(analog_sensor_reading_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  const float base_temp = sinf(mock_step()) * 160.0f + 140.0f;
  const float noisy_temp = base_temp + uniform_noise(0.50f);
  const float smoothed_temp = analog_apply_temp_smoothing(noisy_temp);

  const float base_psi = sinf(mock_step()) * 50.0f + 50.0f;
  const float noisy_psi = base_psi + uniform_noise(1.5f);

  out->oil_temp_f = smoothed_temp;
  out->oil_pressure_psi = (float)analog_round_to_i16(noisy_psi);
  return ESP_OK;
}

static void mock_deinit(void) {}

static const analog_sensor_backend_t mock_backend = {
    .init = mock_init,
    .read = mock_read,
    .deinit = mock_deinit,
};

const analog_sensor_backend_t *analog_sensors_mock_backend(void) { return &mock_backend; }
