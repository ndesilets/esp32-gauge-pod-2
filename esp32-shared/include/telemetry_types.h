#pragma once
#include <stdint.h>

typedef struct {
  // metadata
  uint32_t sequence;
  uint32_t timestamp_ms;

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
} display_packet_t;

typedef struct {
  // metadata
  uint32_t sequence;
  uint32_t timestamp_ms;

  float throttle_pos;
  float brake_pressure_bar;
  float steering_angle_deg;
  float engine_rpm;
} bt_packet_t;