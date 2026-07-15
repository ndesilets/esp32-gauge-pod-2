#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_types.h"

#define ISOTP_SINGLE_FRAME 0x00
#define ISOTP_FIRST_FRAME 0x10
#define ISOTP_CONSECUTIVE_FRAME 0x20
#define ISOTP_FLOW_CONTROL_FRAME 0x30

bool isotp_wrap_payload(const uint8_t* payload, uint16_t payload_len, uint8_t frames[][8], size_t max_frames,
                        size_t* out_frame_count);
bool isotp_unwrap_frames(const can_rx_frame_t frames[], size_t frame_count, uint8_t* out_payload,
                         size_t out_payload_size, size_t* out_payload_len);
