/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "cbor.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "sdkconfig.h"
#include "telemetry_types.h"

static const char* TAG = "data_hub";
static SemaphoreHandle_t device_disconnected_sem;

#define SLIP_END 0xC0
#define SLIP_ESC 0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

int wrap_range(int counter, int lo, int hi) {
  if (hi <= lo) {
    return lo;  // or handle as error
  }
  int span = hi - lo + 1;
  int offset = counter % span;
  if (offset < 0) {
    offset += span;  // guard against negative counters
  }
  return lo + offset;
}

float map_sine_to_range(float sine_val, float lo, float hi) {
  if (hi <= lo) {
    return lo;  // or handle as error
  }
  // sine_val expected in [-1, 1]
  float normalized = (sine_val + 1.0f) * 0.5f;  // now 0..1
  return lo + normalized * (hi - lo);
}

void encode_telemetry_packet(const telemetry_packet_t* packet, uint8_t* buffer, size_t buffer_size,
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

bool slip_encode(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size, size_t* encoded_size) {
  size_t out_idx = 0;

  if (out_idx >= output_size) {
    return false;
  }
  output[out_idx++] = SLIP_END;

  for (size_t i = 0; i < input_size; i++) {
    uint8_t byte = input[i];
    if (byte == SLIP_END) {
      if (out_idx + 2 > output_size) {
        return false;
      }
      output[out_idx++] = SLIP_ESC;
      output[out_idx++] = SLIP_ESC_END;
    } else if (byte == SLIP_ESC) {
      if (out_idx + 2 > output_size) {
        return false;
      }
      output[out_idx++] = SLIP_ESC;
      output[out_idx++] = SLIP_ESC_ESC;
    } else {
      if (out_idx + 1 > output_size) {
        return false;
      }
      output[out_idx++] = byte;
    }
  }

  if (out_idx >= output_size) {
    return false;
  }
  output[out_idx++] = SLIP_END;
  *encoded_size = out_idx;
  return true;
}

void do_things_task(void* arg) {
  const TickType_t period_ticks = pdMS_TO_TICKS(33);  // ~30hz
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t sequence = 0;

  // uart config

  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr_alloc_flags = 0;

  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 512, 512, 10, &uart_queue, intr_alloc_flags));

  for (;;) {
    telemetry_packet_t packet = {
        .sequence = sequence,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,

        // primary
        .water_temp = wrap_range(sequence, -10, 240),
        .oil_temp = wrap_range(sequence, -10, 300),
        .oil_pressure = wrap_range(sequence, 0, 100),

        .dam = map_sine_to_range(sinf(sequence / 50.0f), 0, 1.049),
        .af_learned = map_sine_to_range(sinf(sequence / 50.0f), -10, 10),
        .af_ratio = map_sine_to_range(sinf(sequence / 50.0f), 11.1, 20.0),
        .int_temp = map_sine_to_range(sinf(sequence / 50.0f), 30, 120),

        .fb_knock = map_sine_to_range(sinf(sequence / 50.0f), -6, 0.49),
        .af_correct = map_sine_to_range(sinf(sequence / 50.0f), -10, 10),
        .inj_duty = map_sine_to_range(sinf(sequence / 50.0f), 0, 105),
        .eth_conc = map_sine_to_range(sinf(sequence / 50.0f), 10, 85),

        // supplemental
        .engine_rpm = wrap_range(sequence, 700, 7000),
    };

    uint8_t cbor_buffer[128];  // base msg size is 56 bytes
    size_t encoded_size = 0;
    encode_telemetry_packet(&packet, cbor_buffer, sizeof(cbor_buffer), &encoded_size);
    cbor_buffer[encoded_size] = '\0';

    // uint8_t slip_buffer[260];
    // size_t slip_size = 0;
    // if (!slip_encode(cbor_buffer, encoded_size, slip_buffer, sizeof(slip_buffer), &slip_size)) {
    //   ESP_LOGE(TAG, "SLIP encoding failed; dropping packet");
    //   vTaskDelayUntil(&last_wake, period_ticks);
    //   continue;
    // }

    uart_write_bytes(UART_NUM_1, cbor_buffer, encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
      printf("%02X ", cbor_buffer[i]);
    }
    printf("\n\n");

    sequence++;
    vTaskDelayUntil(&last_wake, period_ticks);
  }
}

void app_main(void) {
  printf("Hello world!\n");

  // everything else

  xTaskCreate(do_things_task, "do_things", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
