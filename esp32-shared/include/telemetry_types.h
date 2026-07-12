#pragma once
#include <stdint.h>

typedef struct {
  // metadata
  uint32_t sequence;
  uint32_t timestamp_ms;

  // primary
  float water_temp;
  float oil_temp;
  float oil_pressure;      // filtered PSI used by the display and monitoring
  float oil_pressure_raw;  // calibrated but unfiltered PSI for diagnostics

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
  float throttle_pos;
  float brake_pressure_bar;
  float steering_angle_deg;
} vehicle_state_t;
