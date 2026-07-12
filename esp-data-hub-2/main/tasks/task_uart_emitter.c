#include "task_uart_emitter.h"

#include "app_context.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "telemetry_protocol.h"

static const char* TAG = "task_uart_emitter";
#define DH_UART_PORT ((uart_port_t)CONFIG_DH_UART_PORT)

void task_uart_emitter(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL) {
    vTaskDelete(NULL);
    return;
  }

  const TickType_t period_ticks = pdMS_TO_TICKS(CONFIG_DH_UART_EMIT_PERIOD_MS);
  TickType_t last_wake = xTaskGetTickCount();

  while (1) {
    vTaskDelayUntil(&last_wake, period_ticks);

    vehicle_state_t state_copy;
    if (xSemaphoreTake(app->vehicle_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state_copy = app->vehicle_state;
      app->vehicle_state.sequence++;
      xSemaphoreGive(app->vehicle_state_mutex);
    } else {
      ESP_LOGW(TAG, "failed to take vehicle_state_mutex");
      continue;
    }

    state_copy.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    uint8_t wire_frame[TELEMETRY_WIRE_FRAME_MAX_SIZE];
    size_t frame_length = 0;
    telemetry_result_t result = telemetry_frame_encode(&state_copy, wire_frame, sizeof(wire_frame) - 1,
                                                       &frame_length);
    if (result != TELEMETRY_RESULT_OK) {
      ESP_LOGW(TAG, "telemetry encode failed: %s", telemetry_result_name(result));
      continue;
    }

    wire_frame[frame_length++] = 0x00;
    const int bytes_written = uart_write_bytes(DH_UART_PORT, wire_frame, frame_length);
    if (bytes_written != (int)frame_length) {
      ESP_LOGW(TAG, "UART short write: expected=%u actual=%d", (unsigned)frame_length, bytes_written);
    }
  }
}
