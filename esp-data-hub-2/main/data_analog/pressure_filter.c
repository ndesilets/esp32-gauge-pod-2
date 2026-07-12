#include "pressure_filter.h"

#include <math.h>
#include <string.h>

static float median_of_three(float a, float b, float c) {
  if (a > b) {
    const float tmp = a;
    a = b;
    b = tmp;
  }
  if (b > c) {
    const float tmp = b;
    b = c;
    c = tmp;
  }
  if (a > b) {
    const float tmp = a;
    a = b;
    b = tmp;
  }
  return b;
}

static float alpha_from_time_constant(uint32_t sample_period_ms, uint32_t time_constant_ms) {
  return 1.0f - expf(-((float)sample_period_ms / (float)time_constant_ms));
}

bool pressure_filter_init(pressure_filter_t* filter, const pressure_filter_config_t* config) {
  if (filter == NULL || config == NULL || config->sample_period_ms == 0 ||
      config->normal_time_constant_ms == 0 || config->fast_time_constant_ms == 0 ||
      config->fast_step_psi <= 0.0f || config->immediate_low_psi < 0.0f) {
    return false;
  }

  memset(filter, 0, sizeof(*filter));
  filter->normal_alpha =
      alpha_from_time_constant(config->sample_period_ms, config->normal_time_constant_ms);
  filter->fast_alpha = alpha_from_time_constant(config->sample_period_ms, config->fast_time_constant_ms);
  filter->fast_step_psi = config->fast_step_psi;
  filter->immediate_low_psi = config->immediate_low_psi;
  filter->fast_sample_count =
      (config->fast_hold_ms + config->sample_period_ms - 1U) / config->sample_period_ms;
  return true;
}

float pressure_filter_apply(pressure_filter_t* filter, float pressure_psi) {
  if (filter == NULL) {
    return pressure_psi;
  }

  if (!filter->initialized) {
    filter->samples[0] = pressure_psi;
    filter->samples[1] = pressure_psi;
    filter->samples[2] = pressure_psi;
    filter->output = pressure_psi;
    filter->initialized = true;
    return pressure_psi;
  }

  filter->samples[filter->next_sample] = pressure_psi;
  filter->next_sample = (uint8_t)((filter->next_sample + 1U) % 3U);

  const float median = median_of_three(filter->samples[0], filter->samples[1], filter->samples[2]);
  if (median <= filter->immediate_low_psi) {
    filter->output = median;
    filter->fast_samples_remaining = 0;
    return filter->output;
  }

  const float delta = median - filter->output;
  if (fabsf(delta) >= filter->fast_step_psi) {
    filter->fast_samples_remaining = filter->fast_sample_count;
  }

  float alpha = filter->normal_alpha;
  if (filter->fast_samples_remaining > 0) {
    alpha = filter->fast_alpha;
    filter->fast_samples_remaining--;
  }

  filter->output += alpha * delta;
  if (filter->output < 0.0f) {
    filter->output = 0.0f;
  } else if (filter->output > 100.0f) {
    filter->output = 100.0f;
  }
  return filter->output;
}
