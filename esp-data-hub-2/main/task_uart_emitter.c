#include "task_uart_emitter.h"

#include "app_context.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "telemetry_codec.h"

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

    telemetry_packet_t state_copy;
    if (xSemaphoreTake(app->current_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state_copy = app->current_state;
      app->current_state.sequence++;
      xSemaphoreGive(app->current_state_mutex);
    } else {
      ESP_LOGW(TAG, "failed to take current_state_mutex");
      continue;
    }

    uint8_t cbor_buffer[128];  // base msg size is 56 bytes
    size_t encoded_size = 0;
    telemetry_codec_encode_packet(&state_copy, cbor_buffer, sizeof(cbor_buffer), &encoded_size);
    cbor_buffer[encoded_size] = '\0';

    uart_write_bytes(DH_UART_PORT, cbor_buffer, encoded_size);
  }
}
