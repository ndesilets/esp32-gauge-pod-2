#include "task_analog_sensors.h"

#include "analog_sensors.h"
#include "app_context.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char* TAG = "task_analog_sensors";

void task_analog_sensors(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL) {
    vTaskDelete(NULL);
    return;
  }

  esp_err_t init_err = analog_sensors_init();
  if (init_err != ESP_OK) {
    ESP_LOGW(TAG, "analog sensors init failed: %s", esp_err_to_name(init_err));
  }

  TickType_t last_log_tick = xTaskGetTickCount();

  while (1) {
    if (init_err != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      init_err = analog_sensors_init();
      if (init_err != ESP_OK) {
        ESP_LOGW(TAG, "analog sensors re-init failed: %s", esp_err_to_name(init_err));
        continue;
      }
      ESP_LOGI(TAG, "analog sensors init recovered");
    }

    analog_sensor_reading_t reading = {0};
    esp_err_t read_err = analog_sensors_read(&reading);
    if (read_err != ESP_OK) {
      ESP_LOGW(TAG, "analog sensors read failed: %s", esp_err_to_name(read_err));
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    if (xSemaphoreTake(app->display_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      app->display_state.oil_temp = reading.oil_temp_f;
      app->display_state.oil_pressure = reading.oil_pressure_psi;
      xSemaphoreGive(app->display_state_mutex);
    } else {
      ESP_LOGW(TAG, "analog task failed to take display_state_mutex");
    }

    TickType_t now = xTaskGetTickCount();
    if ((now - last_log_tick) >= pdMS_TO_TICKS(CONFIG_DH_ANALOG_LOG_PERIOD_MS)) {
      last_log_tick = now;
      // ESP_LOGI(TAG, "analog oil_temp=%.1fF oil_pressure=%.1fpsi", reading.oil_temp_f, reading.oil_pressure_psi);
    }

    vTaskDelay(pdMS_TO_TICKS(CONFIG_DH_ANALOG_POLL_PERIOD_MS));
  }
}
