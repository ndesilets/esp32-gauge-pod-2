#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "pressure_filter.h"

static const pressure_filter_config_t default_config = {
    .sample_period_ms = 20,
    .normal_time_constant_ms = 180,
    .fast_time_constant_ms = 22,
    .fast_step_psi = 8.0f,
    .fast_hold_ms = 60,
    .immediate_low_psi = 3.0f,
};

static pressure_filter_t new_filter(void) {
  pressure_filter_t filter;
  assert(pressure_filter_init(&filter, &default_config));
  return filter;
}

static void test_rejects_invalid_config(void) {
  pressure_filter_t filter;
  pressure_filter_config_t invalid = default_config;
  invalid.sample_period_ms = 0;
  assert(!pressure_filter_init(&filter, &invalid));
  assert(!pressure_filter_init(NULL, &default_config));
  assert(!pressure_filter_init(&filter, NULL));
}

static void test_initializes_at_first_sample(void) {
  pressure_filter_t filter = new_filter();
  assert(pressure_filter_apply(&filter, 42.5f) == 42.5f);
  assert(pressure_filter_apply(&filter, 42.5f) == 42.5f);
}

static void test_reinitialization_discards_history(void) {
  pressure_filter_t filter = new_filter();
  pressure_filter_apply(&filter, 60.0f);
  pressure_filter_apply(&filter, 40.0f);
  pressure_filter_apply(&filter, 40.0f);

  assert(pressure_filter_init(&filter, &default_config));
  assert(pressure_filter_apply(&filter, 75.0f) == 75.0f);
}

static void test_rejects_one_sample_spike(void) {
  pressure_filter_t filter = new_filter();
  assert(pressure_filter_apply(&filter, 60.0f) == 60.0f);
  assert(pressure_filter_apply(&filter, 85.0f) == 60.0f);
  assert(pressure_filter_apply(&filter, 60.0f) == 60.0f);
}

static void test_tracks_large_drop_quickly(void) {
  pressure_filter_t filter = new_filter();
  pressure_filter_apply(&filter, 60.0f);

  // The first changed sample is rejected by the median window.
  assert(pressure_filter_apply(&filter, 40.0f) == 60.0f);

  // The second changed sample confirms the step and starts fast response.
  float output = pressure_filter_apply(&filter, 40.0f);
  assert(output < 50.0f);

  for (int i = 0; i < 3; ++i) {
    output = pressure_filter_apply(&filter, 40.0f);
  }
  assert(fabsf(output - 40.0f) < 1.0f);
}

static void test_near_zero_bypasses_exponential_filter(void) {
  pressure_filter_t filter = new_filter();
  pressure_filter_apply(&filter, 60.0f);
  assert(pressure_filter_apply(&filter, 0.0f) == 60.0f);
  assert(pressure_filter_apply(&filter, 0.0f) == 0.0f);
}

static void test_attenuates_small_continuous_noise(void) {
  pressure_filter_t filter = new_filter();
  pressure_filter_apply(&filter, 60.0f);

  float min_output = 60.0f;
  float max_output = 60.0f;
  for (int i = 0; i < 100; ++i) {
    const float input = (i % 2 == 0) ? 55.0f : 65.0f;
    const float output = pressure_filter_apply(&filter, input);
    if (output < min_output) min_output = output;
    if (output > max_output) max_output = output;
  }

  assert(min_output > 57.0f);
  assert(max_output < 63.0f);
}

int main(void) {
  test_rejects_invalid_config();
  test_initializes_at_first_sample();
  test_reinitialization_discards_history();
  test_rejects_one_sample_spike();
  test_tracks_large_drop_quickly();
  test_near_zero_bypasses_exponential_filter();
  test_attenuates_small_continuous_noise();
  puts("pressure filter tests passed");
  return 0;
}
