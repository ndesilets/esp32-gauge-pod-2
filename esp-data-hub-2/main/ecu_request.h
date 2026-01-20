#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ISOTP_MAX_MESSAGE_BYTES
#define ISOTP_MAX_MESSAGE_BYTES 512
#endif

#ifndef ISOTP_BUFFER_SLOTS
#define ISOTP_BUFFER_SLOTS 4
#endif

typedef enum {
  ISOTP_FRAME_SINGLE = 0x0,
  ISOTP_FRAME_FIRST = 0x1,
  ISOTP_FRAME_CONSECUTIVE = 0x2,
  ISOTP_FRAME_FLOW_CONTROL = 0x3
} isotp_frame_type_t;

typedef struct {
  uint32_t timestamp_ms;
  uint32_t can_id;
  const uint8_t* payload;
  size_t payload_len;
} can_raw_msg_t;

typedef struct {
  uint32_t timestamp_ms;
  uint32_t can_id;
  size_t payload_len;
  uint8_t payload[ISOTP_MAX_MESSAGE_BYTES];
} isotp_processed_msg_t;

typedef struct {
  bool in_use;
  uint32_t can_id;
  uint32_t timestamp_ms;
  uint16_t expected_len;
  uint8_t last_seq;
  size_t payload_len;
  uint8_t payload[ISOTP_MAX_MESSAGE_BYTES];
} isotp_buffer_entry_t;

typedef struct {
  isotp_buffer_entry_t slots[ISOTP_BUFFER_SLOTS];
} isotp_context_t;

void isotp_init(isotp_context_t* ctx);

// Returns true when a complete ISO-TP message has been assembled into `out`.
bool isotp_parse_frame(isotp_context_t* ctx, const can_raw_msg_t* raw, isotp_processed_msg_t* out);
