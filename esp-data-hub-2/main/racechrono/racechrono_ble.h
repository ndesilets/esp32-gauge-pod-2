#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Starts the RaceChrono DIY BLE GATT server and begins advertising after the
// NimBLE host has synchronized with the controller.
bool racechrono_ble_init(void);

// Sends one RaceChrono CAN-Bus API packet if the peer is connected, has
// subscribed to notifications, and has enabled the packet ID in its filter.
bool racechrono_ble_notify_packet(uint32_t packet_id, const uint8_t* payload, size_t payload_len);
