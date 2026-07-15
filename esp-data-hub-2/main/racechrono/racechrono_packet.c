#include "racechrono_packet.h"

#include <math.h>

static int32_t round_to_nearest(float value) {
  return value >= 0.0f ? (int32_t)(value + 0.5f) : (int32_t)(value - 0.5f);
}

static uint8_t clamp_u8(float value, uint8_t maximum) {
  if (!isfinite(value) || value <= 0.0f) {
    return 0;
  }
  if (value >= (float)maximum) {
    return maximum;
  }
  return (uint8_t)round_to_nearest(value);
}

static int16_t clamp_i16(float value) {
  if (!isfinite(value)) {
    return 0;
  }
  if (value >= 32767.0f) {
    return INT16_MAX;
  }
  if (value <= -32768.0f) {
    return INT16_MIN;
  }
  return (int16_t)round_to_nearest(value);
}

bool racechrono_packet_encode_vehicle_controls(const vehicle_state_t* state, uint8_t* out,
                                                size_t out_size) {
  if (state == NULL || out == NULL || out_size < RACECHRONO_PACKET_VEHICLE_CONTROLS_SIZE) {
    return false;
  }

  const uint8_t throttle_percent = clamp_u8(state->throttle_pos, 100U);
  const uint8_t brake_pressure_bar = clamp_u8(state->brake_pressure_bar, RACECHRONO_BRAKE_PRESSURE_MAX_BAR);
  const uint16_t steering_angle_deg = (uint16_t)clamp_i16(state->steering_angle_deg);

  out[0] = throttle_percent;
  out[1] = brake_pressure_bar;
  out[2] = (uint8_t)(steering_angle_deg >> 8);
  out[3] = (uint8_t)(steering_angle_deg & 0xFFU);
  return true;
}
