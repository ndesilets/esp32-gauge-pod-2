#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"

#define RACECHRONO_PACKET_ID_VEHICLE_CONTROLS UINT32_C(0x500)
#define RACECHRONO_PACKET_VEHICLE_CONTROLS_SIZE 4U

// Encodes the RaceChrono synthetic CAN packet 0x500 as:
//   byte 0: throttle position, uint8 whole percent (0..100)
//   byte 1: brake pressure, uint8 whole bar (0..150)
//   byte 2: steering angle high byte, signed int16 whole degrees
//   byte 3: steering angle low byte, signed int16 whole degrees
bool racechrono_packet_encode_vehicle_controls(const vehicle_state_t* state, uint8_t* out,
                                                size_t out_size);
