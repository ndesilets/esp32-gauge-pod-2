/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "analog_sensors.h"
#include "cbor.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
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
#define DH_UART_PORT ((uart_port_t)CONFIG_DH_UART_PORT)

telemetry_packet_t current_state = {};
SemaphoreHandle_t current_state_mutex;

#define ECU_REQ_ID 0x7E0
#define ECU_RES_ID 0x7E8
#define ABS_REQ_ID 0x7B0
#define ABS_RES_ID 0x7B8  // TODO: this is probably more accurate as VDC

// --- twai queues

typedef struct {
  uint32_t id;
  bool ide;
  uint8_t data[8];
  uint8_t data_len;
} can_rx_frame_t;

#define ISOTP_FC_BS_LET_ER_EAT_BUD 0x00
#define ISOTP_FC_BS_CONTINUE 0x01
#define ISOTP_FC_BS_WAIT 0x02
#define ISOTP_FC_BS_ABORT 0x03

#define ISOTP_SINGLE_FRAME 0x00
#define ISOTP_FIRST_FRAME 0x10
#define ISOTP_CONSECUTIVE_FRAME 0x20
#define ISOTP_FLOW_CONTROL_FRAME 0x30

typedef struct {
  uint32_t source_id;
  size_t len;
  uint8_t data[128];
} assembled_isotp_t;

static QueueHandle_t can_rx_queue = NULL;           // raw can frames
static QueueHandle_t ecu_can_frames = NULL;         // can frames from 0x7E8
static QueueHandle_t abs_can_frames = NULL;         // can frames from 0x7B8
static QueueHandle_t assembled_isotp_queue = NULL;  // assembled isotp messages

void debug_log_frame(bool isRx, const uint8_t* buffer, size_t length) {
  static const char hex_chars[] = "0123456789ABCDEF";
  const size_t max_len = (length > 64) ? 64 : length;
  char hex_out[3 * 64 + 1];
  size_t out_idx = 0;

  for (size_t i = 0; i < max_len; i++) {
    uint8_t byte = buffer[i];
    hex_out[out_idx++] = hex_chars[byte >> 4];
    hex_out[out_idx++] = hex_chars[byte & 0x0F];
    if (i + 1 < max_len) {
      hex_out[out_idx++] = ' ';
    }
  }
  hex_out[out_idx] = '\0';

  ESP_LOGI(TAG, "%s %s", isRx ? "[RX]" : "[TX]", hex_out);
}

// --- cbor encoder

// cbor encode telemetry packet into buffer, returns size used
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

    uart_write_bytes(DH_UART_PORT, cbor_buffer, encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
      printf("%02X ", cbor_buffer[i]);
    }
    printf("\n\n");

    sequence++;
    vTaskDelayUntil(&last_wake, period_ticks);
  }
}

// --- TWAI rx callback

// takes raw CAN frames and pushes them to a queue for further processing outside of an ISR
static bool twai_rx_cb(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx) {
  (void)edata;
  QueueHandle_t rx_queue = (QueueHandle_t)user_ctx;
  BaseType_t high_task_woken = pdFALSE;

  uint8_t rx_buf[8] = {0};
  twai_frame_t rx_frame = {
      .buffer = rx_buf,
      .buffer_len = sizeof(rx_buf),
  };
  if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
    // TODO some mechanism of reporting errors/dropped frames
    return false;
  }

  if (rx_frame.header.id != 0x7E8 && rx_frame.header.id != 0x7B8) {
    // not an id we care about
    return false;
  }

  can_rx_frame_t out = {
      .id = rx_frame.header.id,
      .ide = rx_frame.header.ide,
      .data_len = rx_frame.header.dlc,
  };
  memcpy(out.data, rx_buf, out.data_len);
  xQueueSendFromISR(rx_queue, &out, &high_task_woken);

  return (high_task_woken == pdTRUE);
}

// --- analog sensor stuff

void analog_data_task(void* arg) {
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

    if (xSemaphoreTake(current_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      current_state.oil_temp = reading.oil_temp_f;
      current_state.oil_pressure = reading.oil_pressure_psi;
      xSemaphoreGive(current_state_mutex);
    } else {
      ESP_LOGW(TAG, "analog task failed to take current_state_mutex");
    }

    TickType_t now = xTaskGetTickCount();
    if ((now - last_log_tick) >= pdMS_TO_TICKS(CONFIG_DH_ANALOG_LOG_PERIOD_MS)) {
      last_log_tick = now;
      ESP_LOGI(TAG, "analog oil_temp=%.1fF oil_pressure=%.1fpsi", reading.oil_temp_f, reading.oil_pressure_psi);
    }

    vTaskDelay(pdMS_TO_TICKS(CONFIG_DH_ANALOG_POLL_PERIOD_MS));
  }
}

// --- ssm/uds stuff

/*
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

/*
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
*/

// TODO figure out better way of having telemetry_type_t and this since they're largely the same
typedef struct {
  // primary
  float water_temp;
  float oil_temp;
  float oil_pressure;

  float dam;
  float af_learned;
  float af_ratio;
  float int_temp;

  float fb_knock;
  float af_correct;
  float inj_duty;
  float eth_conc;

  // supplemental
  float engine_rpm;
} ssm_ecu_response_t;

static inline float ssm_ecu_parse_coolant_temp(uint8_t value) { return 32.0f + 9.0f * ((float)value - 40.0f) / 5.0f; }

static inline float ssm_ecu_parse_af_correction(uint8_t value) { return ((float)value - 128.0f) * 100.0f / 128.0f; }

static inline float ssm_ecu_parse_af_learning(uint8_t value) { return ((float)value - 128.0f) * 100.0f / 128.0f; }

static inline float ssm_ecu_parse_rpm(uint16_t value) { return (float)value / 4.0f; }

static inline float ssm_ecu_parse_intake_air_temp(uint8_t value) {
  return 32.0f + 9.0f * ((float)value - 40.0f) / 5.0f;
}

static inline float ssm_ecu_parse_afr(uint8_t value) { return (float)value * 14.7f / 128.0f; }

static inline float ssm_ecu_parse_dam(uint32_t value) {
  float out;
  memcpy(&out, &value, sizeof(out));
  return out;
}

static inline float ssm_ecu_parse_feedback_knock(uint32_t value) {
  float out;
  memcpy(&out, &value, sizeof(out));
  return out;
}

/*
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
*/

void ssm_parse_message(uint8_t ssm_payload[], uint8_t length, ssm_ecu_response_t* response) {
  // ssm_payload structure doesn't change
  response->water_temp = ssm_ecu_parse_coolant_temp(ssm_payload[0]);
  response->af_correct = ssm_ecu_parse_af_correction(ssm_payload[1]);
  response->af_learned = ssm_ecu_parse_af_learning(ssm_payload[2]);
  response->engine_rpm = ssm_ecu_parse_rpm((uint16_t)((ssm_payload[3] << 8) | ssm_payload[4]));
  response->int_temp = ssm_ecu_parse_intake_air_temp(ssm_payload[5]);
  response->af_ratio = ssm_ecu_parse_afr(ssm_payload[6]);
  response->dam = ssm_ecu_parse_dam((uint32_t)(ssm_payload[7] << 24 | ssm_payload[8] << 16 | ssm_payload[9]) << 8 |
                                    ssm_payload[10]);
  response->fb_knock = ssm_ecu_parse_feedback_knock(
      (uint32_t)(ssm_payload[11] << 24 | ssm_payload[12] << 16 | ssm_payload[13] << 8 | ssm_payload[14]));
}

// --- iso-tp stuff

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
 */

// sends single FC frame specifying full speed no delay
static void send_isotp_flow_control(twai_node_handle_t node_hdl, uint32_t to) {
  uint8_t fc_data[3] = {ISOTP_FLOW_CONTROL_FRAME, 0, 0};  // let 'er eat bud
  twai_frame_t fc_msg = {
      .header.id = to,
      .header.ide = false,
      .buffer = fc_data,
      .buffer_len = sizeof(fc_data),
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &fc_msg, pdMS_TO_TICKS(100)));
}

// generates iso-tp framed messages for a given payload. can frame buffer should probably be zeroed out beforehand
static bool isotp_wrap_payload(const uint8_t* payload, uint16_t payload_len, uint8_t frames[][8], size_t max_frames,
                               size_t* out_frame_count) {
  if (!payload || !frames || !out_frame_count || max_frames == 0) {
    return false;
  }

  *out_frame_count = 0;

  if (payload_len <= 7) {
    frames[0][0] = ISOTP_SINGLE_FRAME | (uint8_t)(payload_len & 0x0F);
    memcpy(&frames[0][1], payload, payload_len);
    *out_frame_count = 1;
    return true;
  }

  frames[0][0] = ISOTP_FIRST_FRAME | (uint8_t)((payload_len >> 8) & 0x0F);
  frames[0][1] = (uint8_t)(payload_len & 0xFF);
  memcpy(&frames[0][2], payload, 6);

  size_t offset = 6;
  size_t frame_idx = 1;
  uint8_t seq = 1;
  while (offset < payload_len && frame_idx < max_frames) {
    const size_t remaining = payload_len - offset;
    const size_t chunk = (remaining > 7) ? 7 : remaining;

    frames[frame_idx][0] = ISOTP_CONSECUTIVE_FRAME | (seq & 0x0F);
    memcpy(&frames[frame_idx][1], payload + offset, chunk);

    offset += chunk;
    frame_idx++;
    seq = (seq + 1) & 0x0F;
    if (seq == 0) {
      seq = 1;
    }
  }

  *out_frame_count = frame_idx;
  return (offset >= payload_len);
}

// unwraps ISO-TP frames into a flat payload buffer
static bool isotp_unwrap_frames(const can_rx_frame_t frames[16], size_t frame_count, uint8_t* out_payload,
                                size_t out_payload_size, size_t* out_payload_len) {
  if (!frames || !out_payload || !out_payload_len || frame_count == 0) {
    return false;
  }

  const uint8_t pci = frames[0].data[0];
  const uint8_t type = pci & 0xF0;

  if (type == ISOTP_SINGLE_FRAME) {
    const uint8_t payload_len = pci & 0x0F;
    if (payload_len > 7 || out_payload_size < payload_len) {
      return false;
    }
    memcpy(out_payload, &frames[0].data[1], payload_len);
    *out_payload_len = payload_len;
    return true;
  }

  if (type == ISOTP_FIRST_FRAME) {
    const uint16_t payload_len = ((pci & 0x0F) << 8) | frames[0].data[1];
    if (out_payload_size < payload_len) {
      return false;
    }

    size_t offset = 0;
    memcpy(out_payload, &frames[0].data[2], 6);
    offset += 6;

    uint8_t expected_seq = 1;
    for (size_t i = 1; i < frame_count && offset < payload_len; i++) {
      const uint8_t cf_pci = frames[i].data[0];
      if ((cf_pci & 0xF0) != ISOTP_CONSECUTIVE_FRAME) {
        return false;
      }
      const uint8_t seq = cf_pci & 0x0F;
      if (seq != expected_seq) {
        return false;
      }

      const size_t remaining = payload_len - offset;
      const size_t chunk = (remaining > 7) ? 7 : remaining;
      memcpy(out_payload + offset, &frames[i].data[1], chunk);
      offset += chunk;

      expected_seq = (expected_seq + 1) & 0x0F;
      if (expected_seq == 0) {
        expected_seq = 1;
      }
    }

    if (offset != payload_len) {
      return false;
    }

    *out_payload_len = payload_len;
    return true;
  }

  return false;
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

void transmit_frame(twai_node_handle_t node_hdl, uint16_t dest, uint8_t* buffer, size_t payload_len) {
  twai_frame_t frame = {
      .header.id = dest,
      .header.ide = false,
      .buffer = buffer,
      .buffer_len = payload_len,
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &frame, pdMS_TO_TICKS(100)));
  debug_log_frame(false, frame.buffer, frame.buffer_len);
}

// sends requests for car data, parses responses, and updates state
void car_data_task(void* arg) {
  twai_node_handle_t node_hdl = (twai_node_handle_t)arg;
  const TickType_t poll_period_ticks =
      pdMS_TO_TICKS(CONFIG_DH_CAR_POLL_PERIOD_MS > 0 ? CONFIG_DH_CAR_POLL_PERIOD_MS : 1);

  while (1) {
    vTaskDelay(poll_period_ticks);

    ESP_LOGI(TAG, "--- [START] --- Requesting car data...");

    // drain any stale ECU frames before starting a new request
    can_rx_frame_t stale;
    while (xQueueReceive(ecu_can_frames, &stale, pdMS_TO_TICKS(5)) == pdTRUE) {
      ESP_LOGI(TAG, "Drained stale ECU frame ID 0x%0X", stale.id);
      debug_log_frame(true, stale.data, stale.data_len);
    }

    // --- ECU DATA

    // 1. generate ecu data request

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
    size_t isotp_payload_frame_count = 0;
    if (!isotp_wrap_payload(ssm_req_payload, payload_len, can_frames, 16, &isotp_payload_frame_count)) {
      ESP_LOGE(TAG, "Failed to build ISO-TP frames for ECU data request");
      continue;
    }

    // ESP_LOGI(TAG, "ECU data request will use %zu ISO-TP frames", isotp_payload_frame_count);
    // for (int i = 0; i < isotp_payload_frame_count; i++) {
    //   debug_log_frame(false, can_frames[i], 8);
    // }
    // ESP_LOGI(TAG, "^ Built ISO-TP frames for ECU data request");

    // 2. send first frame of ecu data request

    twai_frame_t msg = {
        .header.id = ECU_REQ_ID,
        .header.ide = false,
        .buffer = can_frames[0],
        // .buffer_len = (isotp_payload_frame_count > 1) ? 8 : (payload_len + 1),
        .buffer_len = 8,  // it's always going to be 8 here
    };
    ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &msg, pdMS_TO_TICKS(100)));
    debug_log_frame(false, msg.buffer, msg.buffer_len);

    ESP_LOGI(TAG, "Sent first message");

    // 3. wait for flow control frame from ecu

    if (isotp_payload_frame_count > 1) {
      can_rx_frame_t frame = {0};
      bool got_fc = false;
      while (!got_fc) {
        if (xQueueReceive(ecu_can_frames, &frame, pdMS_TO_TICKS(1000)) != pdTRUE) {
          ESP_LOGW(TAG, "Didn't receive FC frame from ECU");
          break;
        }

        debug_log_frame(true, frame.data, frame.data_len);

        if ((frame.data[0] & 0xF0) != ISOTP_FLOW_CONTROL_FRAME) {
          ESP_LOGW(TAG, "Unexpected frame from ECU while waiting for FC: 0x%02X", frame.data[0]);
          continue;
        }

        if ((frame.data[0] & 0x0F) != ISOTP_FC_BS_LET_ER_EAT_BUD) {
          ESP_LOGW(TAG, "Unexpected FC frame not telling us to let er eat brother 0x%02X", frame.data[0]);
          continue;
        }

        // debug_log_frame(true, frame.data, frame.data_len);
        ESP_LOGI(TAG, "Got correct FC for request");
        got_fc = true;
      }

      if (!got_fc) {
        continue;
      }
    }

    // 4. send remaining frames of ecu data request (assuming full speed from FC frame)

    if (isotp_payload_frame_count > 1) {
      for (uint8_t i = 1; i < isotp_payload_frame_count; i++) {
        twai_frame_t cf_msg = {
            .header.id = ECU_REQ_ID,
            .header.ide = false,
            .buffer = can_frames[i],
            .buffer_len = 8,
        };
        ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &cf_msg, pdMS_TO_TICKS(100)));
        debug_log_frame(false, cf_msg.buffer, cf_msg.buffer_len);
      }
    }

    ESP_LOGI(TAG, "Sent remaining request frames");

    // 5. wait and gather response frames

    can_rx_frame_t frame = {0};
    while (1) {
      if (xQueueReceive(ecu_can_frames, &frame, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "Didn't receive response from ECU");
        break;
      }
      if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
        ESP_LOGW(TAG, "Skipping FC frame while waiting for ECU response");
        debug_log_frame(true, frame.data, frame.data_len);
        continue;
      }
      break;
    }

    if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
      continue;
    }

    ESP_LOGI(TAG, "Received initial data from ECU");
    debug_log_frame(true, frame.data, frame.data_len);

    // send FC frame if needed

    if ((frame.data[0] & 0xF0) == ISOTP_FIRST_FRAME) {
      send_isotp_flow_control(node_hdl, ECU_REQ_ID);

      ESP_LOGI(TAG, "Sent FC to ECU");
    }

    // 7. assemble response frames into complete message

    assembled_isotp_t assembled = {
        .source_id = ECU_RES_ID,
    };

    if ((frame.data[0] & 0xF0) == ISOTP_SINGLE_FRAME) {
      ESP_LOGI(TAG, "Got single frame");
      debug_log_frame(true, frame.data, frame.data_len);

      assembled.len = frame.data[0] & 0x0F;
      memcpy(assembled.data, &frame.data[1], assembled.len);
    } else if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
      ESP_LOGW(TAG, "Unexpected FC frame when assembling ECU response");
      continue;
    } else {
      assembled.len = ((frame.data[0] & 0x0F) << 8) | frame.data[1];

      can_rx_frame_t frames[16] = {0};
      uint8_t frame_idx = 0;
      frames[frame_idx++] = frame;

      uint8_t remaining = (assembled.len > 6) ? (assembled.len - 6) : 0;
      uint8_t expected_cfs = (remaining + 6) / 7;
      uint8_t expected_frames = 1 + expected_cfs;
      uint8_t max_frames = 16;
      while (frame_idx < expected_frames && frame_idx < max_frames) {
        if (xQueueReceive(ecu_can_frames, &frame, pdMS_TO_TICKS(200)) != pdTRUE) {
          ESP_LOGE(TAG, "Timeout waiting for ECU response frames (%u/%u)", (unsigned)frame_idx,
                   (unsigned)expected_frames);
          break;
        }
        if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
          ESP_LOGW(TAG, "Skipping FC frame during ECU response collection");
          continue;
        }

        ESP_LOGI(TAG, "Got a CF frame");
        debug_log_frame(true, frame.data, frame.data_len);

        frames[frame_idx++] = frame;
      }

      if (!isotp_unwrap_frames(frames, frame_idx, assembled.data, sizeof(assembled.data), &assembled.len)) {
        ESP_LOGE(TAG, "Failed to unwrap ISO-TP frames from ECU");
        continue;
      }
    }

    ESP_LOGI(TAG, "Assembled complete ECU response of %zu bytes:", assembled.len);

    // 8. parse msg.data

    ssm_ecu_response_t response;
    ssm_parse_message(assembled.data, assembled.len, &response);

    if (xSemaphoreTake(current_state_mutex, 0) == pdTRUE) {
      current_state.water_temp = response.water_temp;
      current_state.af_correct = response.af_correct;
      current_state.af_learned = response.af_learned;
      current_state.engine_rpm = response.engine_rpm;
      current_state.int_temp = response.int_temp;
      current_state.af_ratio = response.af_ratio;
      current_state.dam = response.dam;
      current_state.fb_knock = response.fb_knock;

      // TODO log all fields in current_state

      xSemaphoreGive(current_state_mutex);
      ESP_LOGI(TAG,
               "state seq=%" PRIu32 " ts=%" PRIu32
               " water=%.2f oil=%.2f oil_p=%.2f dam=%.3f af_learned=%.2f "
               "af_ratio=%.2f int=%.2f fb_knock=%.2f af_correct=%.2f inj_duty=%.2f eth=%.2f rpm=%.1f",
               current_state.sequence, current_state.timestamp_ms, current_state.water_temp, current_state.oil_temp,
               current_state.oil_pressure, current_state.dam, current_state.af_learned, current_state.af_ratio,
               current_state.int_temp, current_state.fb_knock, current_state.af_correct, current_state.inj_duty,
               current_state.eth_conc, current_state.engine_rpm);
    }

    // --- ABS DATA

    // send_abs_data_request(node_hdl);
    // if (xQueueReceive(assembled_isotp_queue, &msg, portMAX_DELAY) != pdTRUE) {
    //   continue;
    // }

    // TODO parse msg.data
    // TODO update state with abs data
  }
}

// --- can frame dispatcher (pulls from ISR queue and routes to appropriate queues)

void dispatch_can_frame(can_rx_frame_t* frame) {
  switch (frame->id) {
    case ECU_RES_ID:
      xQueueSend(ecu_can_frames, frame, pdMS_TO_TICKS(10));
      break;
    case ABS_RES_ID:
      xQueueSend(abs_can_frames, frame, pdMS_TO_TICKS(10));
      break;
    default:
      ESP_LOGW(TAG, "Unhandled CAN frame ID: 0x%03X", frame->id);
      break;
  }
}

void can_rx_dispatcher_task(void* arg) {
  while (1) {
    can_rx_frame_t frame;
    if (xQueueReceive(can_rx_queue, &frame, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // ESP_LOGI(TAG, "Dispatching CAN frame ID 0x%03X", frame.id);
    // debug_log_frame(true, frame.data, frame.data_len);
    dispatch_can_frame(&frame);

    // drain any backlog without blocking
    while (xQueueReceive(can_rx_queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
      // ESP_LOGI(TAG, "Dispatching CAN frame ID 0x%03X", frame.id);
      // debug_log_frame(true, frame.data, frame.data_len);
      dispatch_can_frame(&frame);
    }
  }
}

// --- uart stuff

void uart_emitter_task(void* arg) {
  const TickType_t period_ticks = pdMS_TO_TICKS(CONFIG_DH_UART_EMIT_PERIOD_MS);
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

    uart_write_bytes(DH_UART_PORT, cbor_buffer, encoded_size);
  }
}

void twai_error_monitor_task(void* arg) {
  twai_node_handle_t node_hdl = (twai_node_handle_t)arg;
  twai_node_status_t last_status = {0};
  twai_node_record_t last_record = {0};
  bool has_last = false;

  while (1) {
    twai_node_status_t status = {0};
    twai_node_record_t record = {0};
    if (twai_node_get_info(node_hdl, &status, &record) == ESP_OK) {
      if (!has_last || status.state != last_status.state || status.tx_error_count != last_status.tx_error_count ||
          status.rx_error_count != last_status.rx_error_count || record.bus_err_num != last_record.bus_err_num) {
        ESP_LOGW(TAG, "TWAI status state=%d tx_err=%u rx_err=%u bus_err=%" PRIu32, status.state, status.tx_error_count,
                 status.rx_error_count, record.bus_err_num);
        last_status = status;
        last_record = record;
        has_last = true;
      }
    } else {
      ESP_LOGW(TAG, "TWAI status read failed");
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// --- TODO bluetooth stuff

// --- main

void app_main(void) {
  printf("Hello world!\n");

  // --- twai/can config

  twai_onchip_node_config_t node_config = {
      .io_cfg.tx = CONFIG_DH_TWAI_TX_GPIO,
      .io_cfg.rx = CONFIG_DH_TWAI_RX_GPIO,
      .bit_timing.bitrate = 500000,
      .tx_queue_depth = 1,  // 8 should be plenty for ecu responses (largest one)

      // TODO test
      // .fail_retry_cnt = 0,
      // .flags.enable_self_test = true,
  };
  twai_node_handle_t node_hdl = NULL;
  ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

  // rx callback + queues

  can_rx_queue = xQueueCreate(16, sizeof(can_rx_frame_t));
  ecu_can_frames = xQueueCreate(16, sizeof(can_rx_frame_t));
  abs_can_frames = xQueueCreate(16, sizeof(can_rx_frame_t));
  assembled_isotp_queue = xQueueCreate(16, sizeof(assembled_isotp_t));

  if (can_rx_queue == NULL || assembled_isotp_queue == NULL || ecu_can_frames == NULL || abs_can_frames == NULL) {
    ESP_LOGE(TAG, "Failed to create TWAI queues");
    return;
  }

  twai_event_callbacks_t twai_cbs = {
      .on_rx_done = twai_rx_cb,
  };
  ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &twai_cbs, can_rx_queue));
  ESP_ERROR_CHECK(twai_node_enable(node_hdl));

  // --- uart config

#ifdef CONFIG_DH_UART_ENABLED
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr_alloc_flags = 0;

  ESP_ERROR_CHECK(uart_param_config(DH_UART_PORT, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(DH_UART_PORT, CONFIG_DH_UART_TX_GPIO, CONFIG_DH_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(uart_driver_install(DH_UART_PORT, CONFIG_DH_UART_BUFFER_SIZE, CONFIG_DH_UART_BUFFER_SIZE, 10,
                                      &uart_queue, intr_alloc_flags));
#endif

  // --- init rest of stuff

  current_state_mutex = xSemaphoreCreateMutex();

  // --- tasks

  xTaskCreate(car_data_task, "car_data_task", 8192 * 2, (void*)node_hdl, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(can_rx_dispatcher_task, "can_rx_dispatcher_task", 8192, NULL, tskIDLE_PRIORITY + 2, NULL);
  xTaskCreate(analog_data_task, "analog_data_task", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(twai_error_monitor_task, "twai_error_monitor_task", 4096, (void*)node_hdl, tskIDLE_PRIORITY + 1, NULL);
#ifdef CONFIG_DH_UART_ENABLED
  xTaskCreate(uart_emitter_task, "uart_emitter_task", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
  // TODO task for ble/gatt server compatible with solostorm etc.
  // apparently racebox uses "uart over ble" in ubx format
}
