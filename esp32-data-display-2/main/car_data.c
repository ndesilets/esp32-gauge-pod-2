#include "car_data.h"

#include <stdbool.h>

#include "esp_timer.h"
#include "math.h"
#include "sdkconfig.h"

#ifdef CONFIG_DD_ENABLE_FAKE_DATA
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

display_packet_t get_data() {
  static int i = 0;

  display_packet_t packet = {
      // metadata
      .sequence = i,
      .timestamp_ms = esp_timer_get_time() / 1000,

      // primary
      // .water_temp = wrap_range(i / 4, 190, 240),
      .oil_temp = wrap_range(i / 4, 200, 300),
      // .oil_pressure = wrap_range(i / 4, 0, 100),

      // .dam = map_sine_to_range(sinf(i / 50.0f), 0, 1.049),
      // .af_learned = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      // .af_ratio = map_sine_to_range(sinf(i / 50.0f), 10.0, 20.0),
      // .int_temp = map_sine_to_range(sinf(i / 50.0f), 0, 120),

      // .fb_knock = map_sine_to_range(sinf(i / 50.0f), -6, 0.49),
      // .af_correct = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      // .inj_duty = map_sine_to_range(sinf(i / 50.0f), 0, 105),
      // .eth_conc = map_sine_to_range(sinf(i / 50.0f), 10, 85),

      // supplemental
      .engine_rpm = 2500.0f,
  };

  i++;

  return packet;
}
#else
#include "cbor.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char* TAG = "car_data";

static bool decode_telemetry_packet(const uint8_t* payload, size_t length, display_packet_t* packet) {
  CborParser parser;
  CborValue root;
  CborError err = cbor_parser_init(payload, length, 0, &parser, &root);
  if (err != CborNoError) {
    ESP_LOGW(TAG, "CBOR parser init failed (%d)", err);
    return false;
  } else if (!cbor_value_is_array(&root)) {
    ESP_LOGW(TAG, "CBOR root is not an array");
    return false;
  }

  size_t array_len = 0;
  err = cbor_value_get_array_length(&root, &array_len);
  if (err != CborNoError || array_len < 14) {
    ESP_LOGW(TAG, "CBOR array length invalid: len=%d err=%d", (int)array_len, err);
    return false;
  }

  CborValue it;
  err = cbor_value_enter_container(&root, &it);
  if (err != CborNoError) {
    ESP_LOGW(TAG, "CBOR enter failed (%d)", err);
    return false;
  }

  CborError field_err = CborNoError;
#define DECODE_UINT_FIELD(field)                   \
  do {                                             \
    uint64_t tmp = 0;                              \
    field_err |= cbor_value_get_uint64(&it, &tmp); \
    packet->field = (uint32_t)tmp;                 \
    field_err |= cbor_value_advance(&it);          \
  } while (0)

#define DECODE_FLOAT_FIELD(field)                 \
  do {                                            \
    float tmp = 0;                                \
    field_err |= cbor_value_get_float(&it, &tmp); \
    packet->field = tmp;                          \
    field_err |= cbor_value_advance(&it);         \
  } while (0)

  DECODE_UINT_FIELD(sequence);
  DECODE_UINT_FIELD(timestamp_ms);

  DECODE_FLOAT_FIELD(water_temp);
  DECODE_FLOAT_FIELD(oil_temp);
  DECODE_FLOAT_FIELD(oil_pressure);

  DECODE_FLOAT_FIELD(dam);
  DECODE_FLOAT_FIELD(af_learned);
  DECODE_FLOAT_FIELD(af_ratio);
  DECODE_FLOAT_FIELD(int_temp);

  DECODE_FLOAT_FIELD(fb_knock);
  DECODE_FLOAT_FIELD(af_correct);
  DECODE_FLOAT_FIELD(inj_duty);
  DECODE_FLOAT_FIELD(eth_conc);

  DECODE_FLOAT_FIELD(engine_rpm);

#undef DECODE_UINT_FIELD
#undef DECODE_FLOAT_FIELD

  field_err |= cbor_value_leave_container(&root, &it);
  if (field_err != CborNoError) {
    ESP_LOGW(TAG, "CBOR decode failed (%d)", field_err);
    return false;
  }

  return true;
}

bool get_data(display_packet_t* packet) {
  uint8_t payload[CONFIG_DD_UART_BUFFER_SIZE];
  int payload_len = 0;

  ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_1, (size_t*)&payload_len));
  if (payload_len <= 0) {
    return false;
  }

  if (payload_len > CONFIG_DD_UART_BUFFER_SIZE) {
    payload_len = CONFIG_DD_UART_BUFFER_SIZE;
  }

  payload_len = uart_read_bytes(UART_NUM_1, payload, payload_len, 100);
  if (payload_len <= 0) {
    return false;
  }

  if (!decode_telemetry_packet(payload, payload_len, packet)) {
    ESP_LOGW(TAG, "could not decode packet");
    return false;
  }

  // ESP_LOGI(TAG, "got packet: sequence=%d timestamp_ms=%d", packet->sequence, packet->timestamp_ms);

  return true;
}
#endif