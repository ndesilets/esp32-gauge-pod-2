#include "request_ecu.h"

#include <string.h>

static inline float ssm_ecu_parse_coolant_temp(uint8_t value) { return 32.0f + 9.0f * ((float)value - 40.0f) / 5.0f; }

static inline float ssm_ecu_parse_af_correction(uint8_t value) { return ((float)value - 128.0f) * 100.0f / 128.0f; }

static inline float ssm_ecu_parse_af_learning(uint8_t value) { return ((float)value - 128.0f) * 100.0f / 128.0f; }

static inline float ssm_ecu_parse_rpm(uint16_t value) { return (float)value / 4.0f; }

static inline float ssm_ecu_parse_intake_air_temp(uint8_t value) {
  return 32.0f + 9.0f * ((float)value - 40.0f) / 5.0f;
}

static inline float ssm_ecu_parse_afr(uint8_t value) { return (float)value * 14.7f / 128.0f; }

static inline float ssm_ecu_parse_dam(uint8_t value) { return (float)value * 0.0625f; }

static inline float ssm_ecu_parse_injector_pw_ms(uint8_t value) { return (float)value * 256.0f / 1000.0f; }

static inline float ssm_ecu_parse_injector_duty(float injector_pw_ms, float engine_rpm) {
  if (engine_rpm <= 0.0f) {
    return 0.0f;
  }

  return injector_pw_ms * engine_rpm / 1200.0f;
}

static inline float ssm_ecu_parse_ethanol_concentration(uint16_t value) { return (float)value * 100.0f / 65536.0f; }

static inline float ssm_ecu_parse_throttle_pos(uint8_t value) { return ((float)value) * 100.0f / 255.0f; }

static inline float ssm_ecu_parse_feedback_knock(uint32_t value) {
  float out;
  memcpy(&out, &value, sizeof(out));
  return out;
}

size_t request_ecu_build_poll_payload(uint8_t* out_payload, size_t out_capacity) {
  if (out_payload == NULL) {
    return 0;
  }

  // clang-format off
  static const uint8_t payload[] = {
      0xA8, 0x00,        // read memory by addr list, padding mode 0
      0x00, 0x00, 0x08,  // coolant
      0x00, 0x00, 0x09,  // af correction #1
      0x00, 0x00, 0x0A,  // af learning #1
      0x00, 0x00, 0x0E,  // engine rpm
      0x00, 0x00, 0x0F,  // engine rpm
      0x00, 0x00, 0x12,  // intake air temperature
      0x00, 0x00, 0x20,  // fuel injector #1 pulse width
      0x00, 0x00, 0x46,  // afr
      0xFF, 0x6B, 0x49,  // DAM
      0xFF, 0x84, 0x80,  // feedback knock correction
      0xFF, 0x84, 0x81,  // feedback knock correction
      0xFF, 0x84, 0x82,  // feedback knock correction
      0xFF, 0x84, 0x83,  // feedback knock correction
      0xFF, 0x1E, 0xE4,  // ethanol concentration
      0xFF, 0x1E, 0xE5,  // ethanol concentration
      0x00, 0x00, 0x29,  // accelerator pedal
  };
  // clang-format on

  if (out_capacity < sizeof(payload)) {
    return 0;
  }

  memcpy(out_payload, payload, sizeof(payload));
  return sizeof(payload);
}

bool request_ecu_parse_ssm_response(const uint8_t* ssm_payload, size_t length, request_ecu_response_t* response) {
  if (ssm_payload == NULL || response == NULL) {
    return false;
  }

  // SSM response payload starts with service id (0xE8).
  if (length < 17 || ssm_payload[0] != 0xE8) {
    return false;
  }

  const uint8_t* data = &ssm_payload[1];

  response->water_temp = ssm_ecu_parse_coolant_temp(data[0]);
  response->af_correct = ssm_ecu_parse_af_correction(data[1]);
  response->af_learned = ssm_ecu_parse_af_learning(data[2]);
  response->engine_rpm = ssm_ecu_parse_rpm((uint16_t)((data[3] << 8) | data[4]));
  response->int_temp = ssm_ecu_parse_intake_air_temp(data[5]);
  response->inj_duty = ssm_ecu_parse_injector_duty(ssm_ecu_parse_injector_pw_ms(data[6]), response->engine_rpm);
  response->af_ratio = ssm_ecu_parse_afr(data[7]);
  response->dam = ssm_ecu_parse_dam(data[8]);
  response->fb_knock = ssm_ecu_parse_feedback_knock((uint32_t)data[9] << 24 | (uint32_t)data[10] << 16 |
                                                    (uint32_t)data[11] << 8 | (uint32_t)data[12]);
  response->eth_conc = ssm_ecu_parse_ethanol_concentration((uint16_t)((data[13] << 8) | data[14]));
  response->throttle_pos = ssm_ecu_parse_throttle_pos(data[15]);

  return true;
}
