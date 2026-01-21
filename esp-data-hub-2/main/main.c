/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cbor.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "math.h"
#include "sdkconfig.h"
#include "telemetry_types.h"

static const char* TAG = "data_hub";

telemetry_packet_t current_state = {};
SemaphoreHandle_t current_state_mutex;

// --- twai queues

typedef struct {
  uint32_t id;
  bool ide;
  uint8_t data[8];
  uint8_t data_len;
} can_rx_frame_t;

typedef struct {
  uint32_t source_id;
  size_t len;
  uint8_t data[128];
} assembled_isotp_t;

static QueueHandle_t can_rx_queue = NULL;
static QueueHandle_t assembled_isotp_queue = NULL;

// --- cbor encoder

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

// --- mock data generation

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

void send_mock_data(void* arg) {
  const TickType_t period_ticks = pdMS_TO_TICKS(33);  // ~30hz
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t sequence = 0;

  // main loop

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

    uart_write_bytes(UART_NUM_1, cbor_buffer, encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
      printf("%02X ", cbor_buffer[i]);
    }
    printf("\n\n");

    sequence++;
    vTaskDelayUntil(&last_wake, period_ticks);
  }
}

// --- analog sensor stuff

void analog_data_task(void* arg) {
  while (1) {
    // TODO read analog sensors and update state
    vTaskDelay(pdMS_TO_TICKS(20));  // ~50hz
  }
}

// --- ssm/uds stuff

/*
ISO-TP notes

CAN IDs
0x7E0 = ecu request
0x7E8 = ecu response

pci_byte = raw_message["payload"][0]
pci_high = (pci_byte & 0xF0) >> 4 # frame type
pci_low = pci_byte & 0x0F # frame specific data (like length or sequence #)

SINGLE_FRAME = 0x0
  pci_low will have length encoded into it
FIRST_FRAME = 0x1
  pci_low will be the MS 4 bits of the data length, first payload byte will be the rest of the 8 bits (12 bits total, up
to 4095 bytes)
  ((pci_low << 8) | payload[1])
CONSECUTIVE_FRAME = 0x2
  pci_low will be the sequence number
FLOW_CONTROL = 0x3
  ecu always seems to indicate full speed, so its probably safe to ignore (famous last words)
  at the very least should check if pci_low != 0 (would indicate wait or overflow)

---

SSM notes

all memory addresses are 24-bit

first byte of payload is service id (ignoring ISO-TP)
    REQ_READ_MEMORY_BY_ADDR_LIST = 0xA8
      list of 24-bit addresses
      second byte is a padding mode but can (probably?) be ignored
      ex: 10 0B [A8 00 00 00 0E 00]
          21 [00 0F 00 00 20 00 00]
    RES_READ_MEMORY_BY_ADDR_LIST = 0xE8
      response of values in requested order, length of values can vary
      ex: 04 [E8 00 00 00 00 00 00]

second byte is subfunction
i think this is pretty much ignored for what i'm working with

---

UDS notes

works in 16-bit addresses and response values

first byte is service ID
0x22 = ReadDataByIdentifier (1 or more values)
  payload is set of 16-bit identifier
  ex: 22 10 29 ...
0x62 = Response to ReadDataByIdentifier
  response is set of 16-bit identifier plus 16-bit value
  ex: 62 10 29 00 01 ...
 */

#define ISOTP_SINGLE_FRAME 0x00
#define ISOTP_FIRST_FRAME 0x10
#define ISOTP_CONSECUTIVE_FRAME 0x20
#define ISOTP_FLOW_CONTROL_FRAME 0x30

static void send_isotp_flow_control(twai_node_handle_t node_hdl) {
  uint8_t fc_data[3] = {ISOTP_FLOW_CONTROL_FRAME, 0, 0};
  twai_frame_t fc_msg = {
      .header.id = 0x7E8,
      .header.ide = false,
      .buffer = fc_data,
      .buffer_len = sizeof(fc_data),
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &fc_msg, pdMS_TO_TICKS(100)));
}

// takes raw CAN frames and pushes them to a queue for further processing outside of an ISR
static bool twai_rx_cb(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx) {
  (void)edata;
  QueueHandle_t rx_queue = (QueueHandle_t)user_ctx;
  BaseType_t high_task_woken = pdFALSE;

  while (1) {
    uint8_t rx_buf[8] = {0};
    twai_frame_t rx_frame = {
        .buffer = rx_buf,
        .buffer_len = sizeof(rx_buf),
    };
    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
      break;
    }

    can_rx_frame_t out = {
        .id = rx_frame.header.id,
        .ide = rx_frame.header.ide,
        .data_len = (rx_frame.header.dlc <= 8) ? rx_frame.header.dlc : 8,
    };
    memcpy(out.data, rx_buf, out.data_len);

    xQueueSendFromISR(rx_queue, &out, &high_task_woken);
  }

  return (high_task_woken == pdTRUE);
}

void send_ecu_data_request(twai_node_handle_t node_hdl) {
  // --- ssm payload

  // ssm payload with no iso-tp framing etc.
  // clang-format off
  uint8_t ssm_req_payload[] = {
      0xA8, 0x00,        // read memory by addr list, padding mode 0
      0x00, 0x00, 0x08,  // coolant
      0x00, 0x00, 0x09,  // af correction #1
      0x00, 0x00, 0x0A,  // af learning #1
      0x00, 0x00, 0x0E,  // engine rpm
      0x00, 0x00, 0x12,  // intake air temperature
      // TODO injector duty cycle
      0x00, 0x00, 0x46,  // afr
      0xFF, 0x6B, 0x49,  // DAM
      0xFF, 0x88, 0x10,  // knock correction
      // TODO ethanol concentration
  };
  // clang-format on
  const int payload_len = sizeof(ssm_req_payload);

  // need to be able to hold 50 / 8 (6.25) individual can payloads + ISOTP overhead
  // so 16 CAN frame slots should be sufficient
  uint8_t can_frames[16][8] = {0};

  // --- assemble iso-tp frames

  // assuming this is only ever going to send multi-frame ISO-TP messages

  // first frame (FF)
  can_frames[0][0] = 0x10 | ((payload_len >> 8) & 0x0F);
  can_frames[0][1] = payload_len & 0xFF;
  memcpy(&can_frames[0][2], &ssm_req_payload[0], 6);

  // consecutive frames (CF)
  int offset = 6;
  int can_frame_idx;
  for (can_frame_idx = 1; offset < payload_len && can_frame_idx < 16; can_frame_idx++) {
    // set chunk size (min of remaining bytes or 7)
    int remaining = payload_len - offset;
    int chunk = (remaining > 7) ? 7 : remaining;

    can_frames[can_frame_idx][0] = 0x20 | (can_frame_idx & 0x0F);
    memcpy(&can_frames[can_frame_idx][1], &ssm_req_payload[offset], chunk);

    offset += chunk;
  }

  // --- send frames over canbus

  for (int i = 0; i < can_frame_idx; i++) {
    twai_frame_t msg = {
        .header.id = 0x7E0,       // Message ID
        .header.ide = false,      // Use 29-bit extended ID format
        .buffer = can_frames[i],  // Pointer to data to transmit
        .buffer_len = 8,          // Length of data to transmit
    };
    ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &msg, pdMS_TO_TICKS(1000)));
  }
}

// assembles isotp message from responses for only one request.
void isotp_assembler_task(void* arg) {
  twai_node_handle_t node_hdl = (twai_node_handle_t)arg;

  uint8_t buffer[128] = {0};
  size_t buffer_len = 0;
  uint16_t payload_length = 0;
  uint8_t last_seq = 0;
  bool in_progress = false;
  TickType_t last_frame_tick = 0;
  const TickType_t frame_timeout = pdMS_TO_TICKS(100);

  while (1) {
    can_rx_frame_t frame;
    if (xQueueReceive(can_rx_queue, &frame, pdMS_TO_TICKS(50)) != pdTRUE) {
      if (in_progress && (xTaskGetTickCount() - last_frame_tick) > frame_timeout) {
        ESP_LOGW(TAG, "ISO-TP timeout, resetting assembly");
        in_progress = false;
        buffer_len = 0;
        payload_length = 0;
        last_seq = 0;
      }
      continue;
    }

    if (frame.id != 0x7E8 && frame.id != 0x7B8) {
      continue;
    }

    uint8_t pci = frame.data[0];
    uint8_t type = pci & 0xF0;
    last_frame_tick = xTaskGetTickCount();

    switch (type) {
      case ISOTP_SINGLE_FRAME:
        uint8_t payload_len = pci & 0x0F;
        if (payload_len > sizeof(buffer)) {
          ESP_LOGW(TAG, "ISO-TP single frame buffer too small");
          continue;
        }
        assembled_isotp_t msg = {
            .source_id = frame.id,
            .len = payload_len,
        };
        memcpy(msg.data, &frame.data[1], payload_len);
        xQueueSend(assembled_isotp_queue, &msg, pdMS_TO_TICKS(10));
        in_progress = false;
        buffer_len = 0;
        payload_length = 0;
        last_seq = 0;
        break;
      case ISOTP_FIRST_FRAME:
        uint8_t pci_low = pci & 0x0F;
        payload_length = ((pci_low << 8) | frame.data[1]);
        if (payload_length > sizeof(buffer)) {
          ESP_LOGW(TAG, "ISO-TP payload too large: %d", payload_length);
          in_progress = false;
          continue;
        }
        buffer_len = 0;
        memcpy(buffer, &frame.data[2], 6);
        buffer_len += 6;
        last_seq = 0;
        in_progress = true;

        send_isotp_flow_control(node_hdl);
        break;
      case ISOTP_CONSECUTIVE_FRAME:
        if (!in_progress) {
          ESP_LOGW(TAG, "ISO-TP consecutive frame without start");
          continue;
        }
        uint8_t seq = pci & 0x0F;
        if (seq != (last_seq + 1) % 16) {
          ESP_LOGW(TAG, "ISO-TP consecutive frame out of order (expected %d, got %d)", (last_seq + 1) % 16, seq);
          in_progress = false;
          buffer_len = 0;
          payload_length = 0;
          last_seq = 0;
          continue;
        }
        last_seq = seq;

        size_t remaining = payload_length - buffer_len;
        size_t copy_len = (remaining > 7) ? 7 : remaining;
        memcpy(buffer + buffer_len, &frame.data[1], copy_len);
        buffer_len += copy_len;

        if (buffer_len >= payload_length) {
          assembled_isotp_t msg = {
              .source_id = frame.id,
              .len = payload_length,
          };
          memcpy(msg.data, buffer, payload_length);
          xQueueSend(assembled_isotp_queue, &msg, pdMS_TO_TICKS(10));
          in_progress = false;
          buffer_len = 0;
          payload_length = 0;
          last_seq = 0;
        }
        break;
      case ISOTP_FLOW_CONTROL_FRAME:
        ESP_LOGW(TAG, "unexpected ISO-TP flow control frame");
        break;
      default:
        ESP_LOGE(TAG, "unexpected ISO-TP frame type");
        break;
    }
  }
}

void send_abs_data_request(twai_node_handle_t node_hdl) {
  // --- uds payload

  // uds payload with no iso-tp framing etc.
  // clang-format off
  uint8_t uds_req_payload[] = {
      0x22,  // read memory by addr list
      0x10, 0x10, // brake pressure
      0x10, 0x29, // steering angle
  };
  // clang-format on

  // --- assemble iso-tp frames

  uint8_t can_frame[8] = {0};
  can_frame[0] = ISOTP_SINGLE_FRAME | (sizeof(uds_req_payload) & 0x0F);
  memcpy(&can_frame[1], &uds_req_payload[0], sizeof(uds_req_payload));

  // --- send frame over canbus

  twai_frame_t msg = {
      .header.id = 0x7B0,   // Message ID
      .header.ide = false,  // Use 29-bit extended ID format
      .buffer = can_frame,  // Pointer to data to transmit
      .buffer_len = 8,      // Length of data to transmit
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &msg, pdMS_TO_TICKS(1000)));
}

void assembled_isotp_processing_task(void* arg) {
  twai_node_handle_t node_hdl = (twai_node_handle_t)arg;

  while (1) {
    send_ecu_data_request(node_hdl);
    assembled_isotp_t msg;
    if (xQueueReceive(assembled_isotp_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // TODO parse msg.data
    // TODO update state with ecu data

    send_abs_data_request(node_hdl);
    if (xQueueReceive(assembled_isotp_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // TODO parse msg.data
    // TODO update state with abs data

    vTaskDelay(pdMS_TO_TICKS(63));  // start with ~16hz, similar to accessport
  }
}

// --- uart stuff

void uart_emitter_task(void* arg) {
  const TickType_t period_ticks = pdMS_TO_TICKS(33);  // ~30hz
  TickType_t last_wake = xTaskGetTickCount();

  // main loop

  while (1) {
    vTaskDelayUntil(&last_wake, period_ticks);

    telemetry_packet_t state_copy;
    if (xSemaphoreTake(current_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      state_copy = current_state;
      current_state.sequence++;
      xSemaphoreGive(current_state_mutex);
    } else {
      // hit timeout
      ESP_LOGW(TAG, "failed to take current_state_mutex");
      continue;
    }

    uint8_t cbor_buffer[128];  // base msg size is 56 bytes
    size_t encoded_size = 0;
    encode_telemetry_packet(&state_copy, cbor_buffer, sizeof(cbor_buffer), &encoded_size);
    cbor_buffer[encoded_size] = '\0';

    uart_write_bytes(UART_NUM_1, cbor_buffer, encoded_size);
    // for (size_t i = 0; i < encoded_size; i++) {
    //   printf("%02X ", cbor_buffer[i]);
    // }
    // printf("\n\n");
  }
}

// --- TODO bluetooth stuff

// --- main

void app_main(void) {
  printf("Hello world!\n");

  // --- twai/can config

  twai_onchip_node_config_t node_config = {
      .io_cfg.tx = 4,
      .io_cfg.rx = 5,
      .bit_timing.bitrate = 500000,
      .tx_queue_depth = 8,  // 8 should be plenty for ecu responses (largest one)
  };
  twai_node_handle_t node_hdl = NULL;
  ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

  // rx callback + queues

  can_rx_queue = xQueueCreate(64, sizeof(can_rx_frame_t));
  assembled_isotp_queue = xQueueCreate(8, sizeof(assembled_isotp_t));
  if (can_rx_queue == NULL || assembled_isotp_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create TWAI queues");
    return;
  }

  twai_event_callbacks_t twai_cbs = {
      .on_rx_done = twai_rx_cb,
  };
  ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &twai_cbs, can_rx_queue));

  ESP_ERROR_CHECK(twai_node_enable(node_hdl));

  // --- uart config

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
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, CONFIG_DH_UART_BUFFER_SIZE, CONFIG_DH_UART_BUFFER_SIZE, 10,
                                      &uart_queue, intr_alloc_flags));

  // --- init rest of stuff

  current_state_mutex = xSemaphoreCreateMutex();

  // --- tasks

  xTaskCreate(analog_data_task, "analog_data_task", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(isotp_assembler_task, "isotp_assembler_task", 8192, (void*)node_hdl, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(assembled_isotp_processing_task, "assembled_isotp_processing_task", 8192, (void*)node_hdl,
              tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(uart_emitter_task, "uart_emitter_task", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);
  // TODO task for ble/gatt server compatible with solostorm etc.
  // apparently racebox uses "uart over ble" in ubx format
}
