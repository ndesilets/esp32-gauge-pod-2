#include "task_vdc_uds.h"

#include <stddef.h>
#include <stdint.h>

#include "app_context.h"
#include "can_types.h"
#include "esp_log.h"
#include "isotp_response.h"
#include "request_vdc.h"
#include "sdkconfig.h"

static const char* TAG = "task_vdc_uds";

void task_vdc_uds(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL || app->node_hdl == NULL) {
    vTaskDelete(NULL);
    return;
  }

  const TickType_t poll_period_ticks =
      pdMS_TO_TICKS(CONFIG_DH_VDC_POLL_PERIOD_MS > 0 ? CONFIG_DH_VDC_POLL_PERIOD_MS : 1);
  TickType_t last_wake = xTaskGetTickCount();

  while (1) {
    vTaskDelayUntil(&last_wake, poll_period_ticks);

    can_rx_frame_t stale;
    while (xQueueReceive(app->vdc_can_frames, &stale, 0) == pdTRUE) {
      ESP_LOGW(TAG, "Drained stale VDC frame ID 0x%0X", stale.id);
    }

    if (!request_vdc_send(app->node_hdl)) {
      continue;
    }

    uint8_t payload[128] = {0};
    size_t payload_len = 0;
    if (!isotp_collect_response(app->vdc_can_frames, app->node_hdl, VDC_REQ_ID, "VDC", TAG, payload,
                                sizeof(payload), &payload_len)) {
      continue;
    }

    float brake_pressure_bar = 0.0f;
    float steering_angle_deg = 0.0f;
    if (!request_vdc_parse_response(payload, payload_len, &brake_pressure_bar, &steering_angle_deg)) {
      ESP_LOGW(TAG, "failed to parse VDC response len=%u sid=0x%02X", (unsigned)payload_len,
               payload_len > 0 ? payload[0] : 0x00);
      continue;
    }

    if (xSemaphoreTake(app->vehicle_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      app->vehicle_state.brake_pressure_bar = brake_pressure_bar;
      app->vehicle_state.steering_angle_deg = steering_angle_deg;
      xSemaphoreGive(app->vehicle_state_mutex);
    } else {
      ESP_LOGW(TAG, "failed to take vehicle_state_mutex");
    }
  }
}
