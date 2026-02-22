#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"

void telemetry_codec_debug_log_frame(bool is_rx, const uint8_t* buffer, size_t length);
void telemetry_codec_encode_packet(const telemetry_packet_t* packet, uint8_t* buffer, size_t buffer_size,
                                   size_t* encoded_size);
