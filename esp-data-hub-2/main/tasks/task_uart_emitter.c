#include "task_uart_emitter.h"

#include "app_context.h"
#include "cbor.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char* TAG = "task_uart_emitter";
#define DH_UART_PORT ((uart_port_t)CONFIG_DH_UART_PORT)

static void encode_display_packet(const display_packet_t* packet, uint8_t* buffer, size_t buffer_size,
                                  size_t* encoded_size) {
  CborEncoder encoder, array_encoder;
  cbor_encoder_init(&encoder, buffer, buffer_size, 0);

  CborError err = cbor_encoder_create_array(&encoder, &array_encoder, 14);
  err |= cbor_encode_uint(&array_encoder, packet->sequence);
  err |= cbor_encode_uint(&array_encoder, packet->timestamp_ms);

  err |= cbor_encode_float(&array_encoder, packet->water_temp);
  err |= cbor_encode_float(&array_encoder, packet->oil_temp);
  err |= cbor_encode_float(&array_encoder, packet->oil_pressure);

  err |= cbor_encode_float(&array_encoder, packet->dam);
  err |= cbor_encode_float(&array_encoder, packet->af_learned);
  err |= cbor_encode_float(&array_encoder, packet->af_ratio);
  err |= cbor_encode_float(&array_encoder, packet->int_temp);

  err |= cbor_encode_float(&array_encoder, packet->fb_knock);
  err |= cbor_encode_float(&array_encoder, packet->af_correct);
  err |= cbor_encode_float(&array_encoder, packet->inj_duty);
  err |= cbor_encode_float(&array_encoder, packet->eth_conc);

  err |= cbor_encode_float(&array_encoder, packet->engine_rpm);
  err |= cbor_encoder_close_container(&encoder, &array_encoder);

  if (err == CborNoError) {
    *encoded_size = cbor_encoder_get_buffer_size(&encoder, buffer);
  } else {
    *encoded_size = 0;
  }
}

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

    display_packet_t state_copy;
    if (xSemaphoreTake(app->display_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state_copy = app->display_state;
      app->display_state.sequence++;
      xSemaphoreGive(app->display_state_mutex);
    } else {
      ESP_LOGW(TAG, "failed to take display_state_mutex");
      continue;
    }

    uint8_t cbor_buffer[128];  // base msg size is 56 bytes
    size_t encoded_size = 0;
    encode_display_packet(&state_copy, cbor_buffer, sizeof(cbor_buffer), &encoded_size);
    cbor_buffer[encoded_size] = '\0';

    uart_write_bytes(DH_UART_PORT, cbor_buffer, encoded_size);
  }
}
