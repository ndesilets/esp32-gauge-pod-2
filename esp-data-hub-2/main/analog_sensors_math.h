#pragma once

#include <stdint.h>

float analog_interpolate_temperature_f(float resistance_ohms);
float analog_interpolate_pressure_psi(float voltage);
float analog_calculate_resistance_ohms(float v_out, float v_dd, float bias_ohms);
float analog_apply_temp_smoothing(float new_temp_f);
void analog_reset_temp_smoothing(void);

int16_t analog_round_to_i16(float value);
