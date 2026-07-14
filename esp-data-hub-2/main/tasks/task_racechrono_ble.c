#include "task_racechrono_ble.h"

#include "app_context.h"
#include "esp_log.h"
#include "racechrono_ble.h"
#include "racechrono_packet.h"
#include "sdkconfig.h"

static const char* TAG = "task_racechrono_ble";

void task_racechrono_ble(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL) {
    vTaskDelete(NULL);
    return;
  }

  const TickType_t period_ticks = pdMS_TO_TICKS(CONFIG_DH_RACECHRONO_BLE_EMIT_PERIOD_MS);
  TickType_t last_wake = xTaskGetTickCount();
  while (1) {
    vTaskDelayUntil(&last_wake, period_ticks);

    vehicle_state_t state_copy;
    if (xSemaphoreTake(app->vehicle_state_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
      ESP_LOGW(TAG, "failed to take vehicle_state_mutex");
      continue;
    }
    state_copy = app->vehicle_state;
    xSemaphoreGive(app->vehicle_state_mutex);

    uint8_t payload[RACECHRONO_PACKET_VEHICLE_CONTROLS_SIZE];
    if (!racechrono_packet_encode_vehicle_controls(&state_copy, payload, sizeof(payload))) {
      ESP_LOGW(TAG, "failed to encode vehicle controls packet");
      continue;
    }
    racechrono_ble_notify_packet(RACECHRONO_PACKET_ID_VEHICLE_CONTROLS, payload, sizeof(payload));
  }
}
