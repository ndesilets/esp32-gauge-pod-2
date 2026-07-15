#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "request_ecu.h"

static void assert_float_near(float actual, float expected) { assert(fabsf(actual - expected) < 0.0001f); }

static void test_builds_golden_poll_payload(void) {
  static const uint8_t expected[] = {
      0xA8, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x09, 0x00, 0x00, 0x0A, 0x00, 0x00,
      0x0E, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x12, 0x00, 0x00, 0x20, 0x00, 0x00, 0x46,
      0xFF, 0x6B, 0x49, 0xFF, 0x84, 0x80, 0xFF, 0x84, 0x81, 0xFF, 0x84, 0x82, 0xFF,
      0x84, 0x83, 0xFF, 0x1E, 0xE4, 0xFF, 0x1E, 0xE5, 0x00, 0x00, 0x29,
  };
  uint8_t payload[sizeof(expected)] = {0};

  const size_t length = request_ecu_build_poll_payload(payload, sizeof(payload));
  assert(length == sizeof(expected));
  assert(memcmp(payload, expected, sizeof(expected)) == 0);
}

static void test_rejects_invalid_poll_payload_output(void) {
  uint8_t payload[50] = {0};
  assert(request_ecu_build_poll_payload(NULL, sizeof(payload)) == 0);
  assert(request_ecu_build_poll_payload(payload, sizeof(payload) - 1) == 0);
}

static void test_parses_known_ssm_response(void) {
  static const uint8_t payload[] = {
      0xE8,
      90,          // coolant: 122 F
      64,          // AF correction: -50%
      192,         // AF learning: 50%
      0x2E, 0xE0,  // RPM: 3000
      60,          // intake temperature: 68 F
      20,          // injector pulse width: 5.12 ms; duty: 12.8%
      128,         // AFR: 14.7
      16,          // DAM: 1.0
      0xBF, 0xC0, 0x00, 0x00,  // feedback knock: -1.5
      0x80, 0x00,              // ethanol: 50%
      255,                     // throttle: 100%
  };
  request_ecu_response_t response = {0};

  assert(request_ecu_parse_ssm_response(payload, sizeof(payload), &response));
  assert_float_near(response.water_temp, 122.0f);
  assert_float_near(response.af_correct, -50.0f);
  assert_float_near(response.af_learned, 50.0f);
  assert_float_near(response.engine_rpm, 3000.0f);
  assert_float_near(response.int_temp, 68.0f);
  assert_float_near(response.inj_duty, 12.8f);
  assert_float_near(response.af_ratio, 14.7f);
  assert_float_near(response.dam, 1.0f);
  assert_float_near(response.fb_knock, -1.5f);
  assert_float_near(response.eth_conc, 50.0f);
  assert_float_near(response.throttle_pos, 100.0f);
}

static void test_zero_rpm_produces_zero_injector_duty(void) {
  uint8_t payload[17] = {0xE8};
  payload[7] = 255;
  request_ecu_response_t response = {0};

  assert(request_ecu_parse_ssm_response(payload, sizeof(payload), &response));
  assert_float_near(response.engine_rpm, 0.0f);
  assert_float_near(response.inj_duty, 0.0f);
}

static void test_rejects_invalid_ssm_responses(void) {
  uint8_t payload[17] = {0xE8};
  request_ecu_response_t response = {0};

  assert(!request_ecu_parse_ssm_response(NULL, sizeof(payload), &response));
  assert(!request_ecu_parse_ssm_response(payload, sizeof(payload), NULL));
  assert(!request_ecu_parse_ssm_response(payload, sizeof(payload) - 1, &response));
  payload[0] = 0x7F;
  assert(!request_ecu_parse_ssm_response(payload, sizeof(payload), &response));
}

int main(void) {
  test_builds_golden_poll_payload();
  test_rejects_invalid_poll_payload_output();
  test_parses_known_ssm_response();
  test_zero_rpm_produces_zero_injector_duty();
  test_rejects_invalid_ssm_responses();
  puts("Subaru SSM payload tests passed");
  return 0;
}
