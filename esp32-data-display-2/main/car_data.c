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

bool get_data(display_packet_t* packet) {
  if (!packet) {
    return false;
  }

  static int i = 0;

  *packet = (display_packet_t){
      // metadata
      .sequence = i,
      .timestamp_ms = esp_timer_get_time() / 1000,

      // primary
      // .water_temp = wrap_range(i / 4, 190, 240),
      .oil_temp = wrap_range(i / 10, 200, 300),
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

  return true;
}
#else
#include "cbor.h"
#include "cobs.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "car_data";
static const size_t DISPLAY_PACKET_CBOR_ITEMS = 14;

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
  if (err != CborNoError || array_len != DISPLAY_PACKET_CBOR_ITEMS) {
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
#define DECODE_UINT_FIELD(field)                     \
  do {                                               \
    uint64_t tmp = 0;                                \
    field_err |= cbor_value_get_uint64(&it, &tmp);   \
    if (tmp > UINT32_MAX) {                          \
      ESP_LOGW(TAG, "CBOR uint overflow for " #field); \
      return false;                                  \
    }                                                \
    packet->field = (uint32_t)tmp;                   \
    field_err |= cbor_value_advance(&it);            \
  } while (0)

#define DECODE_FLOAT_FIELD(field)                 \
  do {                                            \
    float tmp = 0;                                \
    field_err |= cbor_value_get_float(&it, &tmp); \
    packet->field = tmp;                          \
    field_err |= cbor_value_advance(&it);         \
  } while (0)

  // Keep this order and type mapping aligned with esp-data-hub task_uart_emitter.c::encode_display_packet().
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
  if (!packet) {
    return false;
  }

  // Accumulate bytes until the 0x00 COBS frame delimiter. At 115200 baud a
  // full frame (~60 encoded bytes) arrives in under 6ms, so a 10ms per-byte
  // timeout lets us bail quickly when no data is available while still giving
  // the UART enough time to deliver a complete frame once it starts arriving.
  // Max COBS frame for a 128-byte CBOR payload is 129 bytes.
  uint8_t cobs_buf[132];
  size_t cobs_len = 0;
  bool got_delim = false;

  for (size_t i = 0; i < sizeof(cobs_buf); i++) {
    uint8_t b;
    if (uart_read_bytes(UART_NUM_1, &b, 1, pdMS_TO_TICKS(10)) != 1) {
      break;  // timeout — no more data arriving
    }
    if (b == 0x00) {
      got_delim = true;
      break;
    }
    cobs_buf[cobs_len++] = b;
  }

  if (!got_delim || cobs_len == 0) {
    return false;
  }

  uint8_t payload[128];
  size_t payload_len = 0;
  if (!cobs_decode(cobs_buf, cobs_len, payload, sizeof(payload), &payload_len)) {
    ESP_LOGW(TAG, "COBS decode failed (len=%u)", (unsigned)cobs_len);
    return false;
  }

  if (!decode_telemetry_packet(payload, payload_len, packet)) {
    ESP_LOGW(TAG, "could not decode packet");
    return false;
  }

  return true;
}
#endif
