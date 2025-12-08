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
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"
#include "sdkconfig.h"
#include "telemetry_types.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

#define EXAMPLE_USB_HOST_PRIORITY (20)
#define EXAMPLE_USB_DEVICE_VID (0x303A)
#define EXAMPLE_USB_DEVICE_PID (0x4001)       // 0x303A:0x4001 (TinyUSB CDC device)
#define EXAMPLE_USB_DEVICE_DUAL_PID (0x4002)  // 0x303A:0x4002 (TinyUSB Dual CDC device)
#define EXAMPLE_TX_STRING ("CDC test string!")
#define EXAMPLE_TX_TIMEOUT_MS (1000)

static const char* TAG = "data_hub";
static SemaphoreHandle_t device_disconnected_sem;

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

static void handle_dev_event(const cdc_acm_host_dev_event_data_t* event, void* arg) {
  switch (event->type) {
    case CDC_ACM_HOST_ERROR:
      ESP_LOGE(TAG, "CDC-ACM error has occurred, err_no = %i", event->data.error);
      break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
      ESP_LOGI(TAG, "Device suddenly disconnected");
      ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
      xSemaphoreGive(device_disconnected_sem);
      break;
    case CDC_ACM_HOST_SERIAL_STATE:
      ESP_LOGI(TAG, "Serial state notif 0x%04X", event->data.serial_state.val);
      break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
      ESP_LOGW(TAG, "Unsupported CDC event: %i", event->type);
      break;
  }
}

static void usb_lib_task(void* arg) {
  while (1) {
    // Start handling system events
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "USB: All devices freed");
      // Continue handling USB events to allow device reconnection
    }
  }
}

static bool handle_rx(const uint8_t* data, size_t len, void* arg) {
  ESP_LOGI(TAG, "Data received");
  ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
  return true;
}

void app_main(void) {
  printf("Hello world!\n");

  device_disconnected_sem = xSemaphoreCreateBinary();
  assert(device_disconnected_sem);

  // Install USB Host driver. Should only be called once in entire application

  ESP_LOGI(TAG, "Installing USB Host");
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));

  // Create a task that will handle USB library events

  BaseType_t task_created =
      xTaskCreate(usb_lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), EXAMPLE_USB_HOST_PRIORITY, NULL);
  assert(task_created == pdTRUE);

  ESP_LOGI(TAG, "Installing CDC-ACM driver");
  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

  // for device
  const cdc_acm_host_device_config_t cdc_cfg = {
      .connection_timeout_ms = 1000,
      .out_buffer_size = 512,
      .in_buffer_size = 512,
      .user_arg = NULL,
      .event_cb = handle_dev_event,
      .data_cb = handle_rx,
  };

  xTaskCreate(do_things_task, "do_things", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

  for (;;) {
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    ESP_LOGI(TAG, "Opening CDC ACM device VID:PID=%04X:%04X", EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_PID);
    esp_err_t err = cdc_acm_host_open(EXAMPLE_USB_DEVICE_VID, EXAMPLE_USB_DEVICE_PID, 0, &cdc_cfg, &cdc_dev);
    if (err != ESP_OK) {
      ESP_LOGI(TAG, "Failed to open device");
      continue;
    }
    cdc_acm_host_desc_print(cdc_dev);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
