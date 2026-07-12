#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t sample_period_ms;
  uint32_t normal_time_constant_ms;
  uint32_t fast_time_constant_ms;
  float fast_step_psi;
  uint32_t fast_hold_ms;
  float immediate_low_psi;
} pressure_filter_config_t;

typedef struct {
  float samples[3];
  float output;
  float normal_alpha;
  float fast_alpha;
  float fast_step_psi;
  float immediate_low_psi;
  uint32_t fast_sample_count;
  uint32_t fast_samples_remaining;
  uint8_t next_sample;
  bool initialized;
} pressure_filter_t;

bool pressure_filter_init(pressure_filter_t* filter, const pressure_filter_config_t* config);
float pressure_filter_apply(pressure_filter_t* filter, float pressure_psi);
