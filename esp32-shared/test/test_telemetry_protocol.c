#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cobs.h"
#include "telemetry_protocol.h"

static void assert_packet_equal(const display_packet_t* expected, const display_packet_t* actual) {
  assert(expected->sequence == actual->sequence);
  assert(expected->timestamp_ms == actual->timestamp_ms);
  assert(memcmp(&expected->water_temp, &actual->water_temp, sizeof(float) * 12) == 0);
}

static size_t rebuild_frame(uint8_t* raw, size_t raw_length, uint8_t* frame) {
  const uint16_t crc = telemetry_crc16_ccitt_false(raw, raw_length - 2);
  raw[raw_length - 2] = (uint8_t)(crc >> 8);
  raw[raw_length - 1] = (uint8_t)crc;
  return cobs_encode(raw, raw_length, frame);
}

static void test_crc_check_value(void) {
  static const uint8_t input[] = "123456789";
  assert(telemetry_crc16_ccitt_false(input, sizeof(input) - 1) == 0x29B1U);
}

static void test_round_trip(void) {
  const display_packet_t input = {
      .sequence = UINT32_MAX,
      .timestamp_ms = 0x12345678U,
      .water_temp = 212.5f,
      .oil_temp = 230.25f,
      .oil_pressure = 72.75f,
      .dam = 1.0f,
      .af_learned = -3.125f,
      .af_ratio = 14.7f,
      .int_temp = 98.0f,
      .fb_knock = -1.4f,
      .af_correct = 2.25f,
      .inj_duty = 87.5f,
      .eth_conc = 64.0f,
      .engine_rpm = 6789.5f,
  };

  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);
  assert(frame_length <= sizeof(frame));

  display_packet_t output = {0};
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_OK);
  assert_packet_equal(&input, &output);
}

static void test_golden_messagepack_payload(void) {
  const display_packet_t input = {
      .sequence = 0x12345678U,
      .timestamp_ms = 0x9ABCDEF0U,
  };
  static const uint8_t expected_payload[] = {
      0x9F, 0x01,
      0xCE, 0x12, 0x34, 0x56, 0x78,
      0xCE, 0x9A, 0xBC, 0xDE, 0xF0,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
      0xCA, 0x00, 0x00, 0x00, 0x00,
  };

  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);

  uint8_t raw[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  assert(cobs_decode(frame, frame_length, raw, sizeof(raw), &raw_length));
  assert(raw_length == sizeof(expected_payload) + 2);
  assert(memcmp(raw, expected_payload, sizeof(expected_payload)) == 0);
  const uint16_t crc = telemetry_crc16_ccitt_false(expected_payload, sizeof(expected_payload));
  assert(raw[sizeof(expected_payload)] == (uint8_t)(crc >> 8));
  assert(raw[sizeof(expected_payload) + 1] == (uint8_t)crc);
}

static void test_rejects_corruption_without_modifying_destination(void) {
  const display_packet_t input = {.sequence = 42, .timestamp_ms = 99, .engine_rpm = 2500.0f};
  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);

  uint8_t raw[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  assert(cobs_decode(frame, frame_length, raw, sizeof(raw), &raw_length));
  raw[raw_length / 2] ^= 0x01;
  frame_length = cobs_encode(raw, raw_length, frame);
  const display_packet_t sentinel = {.sequence = 777, .water_temp = -100.0f};
  display_packet_t output = sentinel;
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_CRC_ERROR);
  assert(memcmp(&sentinel, &output, sizeof(output)) == 0);
}

static void test_rejects_wrong_schema(void) {
  const display_packet_t input = {0};
  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);

  uint8_t raw[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  assert(cobs_decode(frame, frame_length, raw, sizeof(raw), &raw_length));
  raw[1] = TELEMETRY_SCHEMA_VERSION + 1;
  frame_length = rebuild_frame(raw, raw_length, frame);

  display_packet_t output = {0};
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_SCHEMA_ERROR);
}

static void test_rejects_invalid_messagepack(void) {
  const display_packet_t input = {0};
  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);

  uint8_t raw[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  assert(cobs_decode(frame, frame_length, raw, sizeof(raw), &raw_length));

  raw[0] = 0x9E;  // The protocol requires a 15-item fixarray (0x9F).
  frame_length = rebuild_frame(raw, raw_length, frame);
  display_packet_t output = {0};
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_MSGPACK_ERROR);

  raw[0] = 0x9F;
  raw[4] = 0xC0;  // First telemetry field must be float32, not nil.
  frame_length = rebuild_frame(raw, raw_length, frame);
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_MSGPACK_ERROR);
}

static void test_rejects_trailing_messagepack_data(void) {
  const display_packet_t input = {0};
  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 0;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame), &frame_length) == TELEMETRY_RESULT_OK);

  uint8_t raw[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  assert(cobs_decode(frame, frame_length, raw, sizeof(raw), &raw_length));
  assert(raw_length < sizeof(raw));
  memmove(raw + raw_length - 1, raw + raw_length - 2, 2);
  raw[raw_length - 2] = 0xC0;
  raw_length++;
  frame_length = rebuild_frame(raw, raw_length, frame);

  display_packet_t output = {0};
  assert(telemetry_frame_decode(frame, frame_length, &output) == TELEMETRY_RESULT_MSGPACK_ERROR);
}

static void test_argument_and_size_errors(void) {
  const display_packet_t input = {0};
  uint8_t frame[TELEMETRY_COBS_FRAME_MAX_SIZE];
  size_t frame_length = 123;
  assert(telemetry_frame_encode(&input, frame, sizeof(frame) - 1, &frame_length) ==
         TELEMETRY_RESULT_OUTPUT_TOO_SMALL);
  assert(frame_length == 0);
  assert(telemetry_frame_decode(frame, sizeof(frame) + 1, (display_packet_t*)&input) ==
         TELEMETRY_RESULT_FRAME_TOO_LARGE);
  assert(telemetry_frame_decode(NULL, 0, (display_packet_t*)&input) == TELEMETRY_RESULT_INVALID_ARGUMENT);
  const uint8_t invalid_cobs[] = {0x00};
  assert(telemetry_frame_decode(invalid_cobs, sizeof(invalid_cobs), (display_packet_t*)&input) ==
         TELEMETRY_RESULT_COBS_ERROR);
}

int main(void) {
  test_crc_check_value();
  test_round_trip();
  test_golden_messagepack_payload();
  test_rejects_corruption_without_modifying_destination();
  test_rejects_wrong_schema();
  test_rejects_invalid_messagepack();
  test_rejects_trailing_messagepack_data();
  test_argument_and_size_errors();
  puts("telemetry protocol tests passed");
  return 0;
}
