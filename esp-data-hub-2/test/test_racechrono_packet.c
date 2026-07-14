#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "racechrono_packet.h"

static void test_encodes_whole_number_controls(void) {
  vehicle_state_t state = {
      .throttle_pos = 42.6f,
      .brake_pressure_bar = 87.4f,
      .steering_angle_deg = -123.6f,
  };
  uint8_t packet[RACECHRONO_PACKET_VEHICLE_CONTROLS_SIZE] = {0};
  assert(racechrono_packet_encode_vehicle_controls(&state, packet, sizeof(packet)));
  assert(packet[0] == 43U);
  assert(packet[1] == 87U);
  assert(packet[2] == 0xFFU);
  assert(packet[3] == 0x84U);
}

static void test_clamps_and_rejects_invalid_arguments(void) {
  vehicle_state_t state = {
      .throttle_pos = 250.0f,
      .brake_pressure_bar = 160.0f,
      .steering_angle_deg = NAN,
  };
  uint8_t packet[RACECHRONO_PACKET_VEHICLE_CONTROLS_SIZE] = {0};
  assert(!racechrono_packet_encode_vehicle_controls(NULL, packet, sizeof(packet)));
  assert(!racechrono_packet_encode_vehicle_controls(&state, NULL, sizeof(packet)));
  assert(!racechrono_packet_encode_vehicle_controls(&state, packet, sizeof(packet) - 1U));
  assert(racechrono_packet_encode_vehicle_controls(&state, packet, sizeof(packet)));
  assert(packet[0] == 100U);
  assert(packet[1] == 150U);
  assert(packet[2] == 0U);
  assert(packet[3] == 0U);
}

int main(void) {
  test_encodes_whole_number_controls();
  test_clamps_and_rejects_invalid_arguments();
  puts("RaceChrono packet tests passed");
  return 0;
}
