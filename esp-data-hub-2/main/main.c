/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>

#include "cbor.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "sdkconfig.h"
#include "telemetry_types.h"

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

void do_things_task(void* arg) {
  const TickType_t period_ticks = pdMS_TO_TICKS(33);  // ~30hz
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t sequence = 0;

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

    for (size_t i = 0; i < encoded_size; ++i) {
      printf("%02X", cbor_buffer[i]);
      printf(" ");
    }
    printf("\n");

    sequence++;
    vTaskDelayUntil(&last_wake, period_ticks);
  }
}

void app_main(void) {
  printf("Hello world!\n");

  //   xTaskCreatePinnedToCore(do_things_task, "do_things", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(do_things_task, "do_things_2", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
}
