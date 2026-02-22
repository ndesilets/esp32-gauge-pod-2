#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"

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
} request_ecu_response_t;

size_t request_ecu_build_poll_payload(uint8_t* out_payload, size_t out_capacity);
bool request_ecu_parse_ssm_response(const uint8_t* ssm_payload, size_t length, request_ecu_response_t* response);
void request_ecu_apply_to_telemetry(const request_ecu_response_t* response, display_packet_t* telemetry);
