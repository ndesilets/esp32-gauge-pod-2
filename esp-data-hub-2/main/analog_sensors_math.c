#include "analog_sensors_math.h"

#include <math.h>

static const int32_t rife_temp_sensor_ref[] = {
    189726,  // -20F
    132092,  // -10F
    93425,   // 0F
    67059,   // 10F
    48804,   // 20F
    35983,   // 30F
    26855,   // 40F
    20274,   // 50F
    15473,   // 60F
    11929,   // 70F
    9287,    // 80F
    7295,    // 90F
    5781,    // 100F
    4618,    // 110F
    3718,    // 120F
    3016,    // 130F
    2463,    // 140F
    2025,    // 150F
    1675,    // 160F
    1395,    // 170F
    1167,    // 180F
    983,     // 190F
    832,     // 200F
    707,     // 210F
    604,     // 220F
    519,     // 230F
    447,     // 240F
    387,     // 250F
    336,     // 260F
    294,     // 270F
    257,     // 280F
    226,     // 290F
};
static const int temp_sensor_ref_len = sizeof(rife_temp_sensor_ref) / sizeof(rife_temp_sensor_ref[0]);

static const int alpha_shift = 1;
static int smoothed_temp_q8 = 0;

float analog_interpolate_temperature_f(float resistance_ohms) {
  int hi_res_idx = 0;
  int lo_res_idx = 0;

  for (int i = 0; i < temp_sensor_ref_len; i++) {
    if (resistance_ohms > (float)rife_temp_sensor_ref[i]) {
      if (i == 0) {
        return -20.0f;
      }

      hi_res_idx = i;
      lo_res_idx = i - 1;
      break;
    }

    if (i == temp_sensor_ref_len - 1) {
      return 290.0f;
    }
  }

  const float hi_temp = -20.0f + ((float)hi_res_idx * 10.0f);
  const float lo_temp = hi_temp - 10.0f;

  const float hi_res = (float)rife_temp_sensor_ref[hi_res_idx];
  const float lo_res = (float)rife_temp_sensor_ref[lo_res_idx];

  return hi_temp + ((resistance_ohms - hi_res) / (lo_res - hi_res)) * (lo_temp - hi_temp);
}

float analog_interpolate_pressure_psi(float voltage) {
  if (voltage < 0.5f) {
    return 0.0f;
  }

  if (voltage > 4.5f) {
    return 100.0f;
  }

  return (voltage - 0.5f) * (100.0f / 4.0f);
}

float analog_calculate_resistance_ohms(float v_out, float v_dd, float bias_ohms) {
  if (v_out <= 0.0f) {
    return 0.0f;
  }
  return ((v_dd * bias_ohms) / v_out) - bias_ohms;
}

float analog_apply_temp_smoothing(float new_temp_f) {
  const int unsmoothed = analog_round_to_i16(new_temp_f);
  smoothed_temp_q8 += (((int)unsmoothed << 8) - smoothed_temp_q8) >> alpha_shift;
  return (float)(smoothed_temp_q8 >> 8);
}

void analog_reset_temp_smoothing(void) { smoothed_temp_q8 = 0; }

int16_t analog_round_to_i16(float value) { return (int16_t)lroundf(value); }
