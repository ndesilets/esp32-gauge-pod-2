#pragma once

#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_SCHEMA_VERSION 3U
#define TELEMETRY_MSGPACK_ITEM_COUNT 19U

// Maximum encoded sizes for the current 19-item schema:
//   array16 + version + two uint32 values + sixteen float32 values = 94 bytes
//   raw frame = MessagePack + two-byte CRC
//   COBS frame = raw + raw/254 + one code byte
#define TELEMETRY_MSGPACK_MAX_SIZE 94U
#define TELEMETRY_RAW_FRAME_MAX_SIZE (TELEMETRY_MSGPACK_MAX_SIZE + 2U)
#define TELEMETRY_COBS_FRAME_MAX_SIZE \
  (TELEMETRY_RAW_FRAME_MAX_SIZE + (TELEMETRY_RAW_FRAME_MAX_SIZE / 254U) + 1U)
#define TELEMETRY_WIRE_FRAME_MAX_SIZE (TELEMETRY_COBS_FRAME_MAX_SIZE + 1U)

typedef enum {
  TELEMETRY_RESULT_OK = 0,
  TELEMETRY_RESULT_INVALID_ARGUMENT,
  TELEMETRY_RESULT_OUTPUT_TOO_SMALL,
  TELEMETRY_RESULT_FRAME_TOO_LARGE,
  TELEMETRY_RESULT_COBS_ERROR,
  TELEMETRY_RESULT_CRC_ERROR,
  TELEMETRY_RESULT_MSGPACK_ERROR,
  TELEMETRY_RESULT_SCHEMA_ERROR,
} telemetry_result_t;

// Encodes one complete frame, excluding the trailing 0x00 UART delimiter.
telemetry_result_t telemetry_frame_encode(const vehicle_state_t* packet, uint8_t* output,
                                          size_t output_capacity, size_t* output_length);

// Decodes one COBS frame. `frame` must not include the trailing 0x00 delimiter.
// `packet` is only modified after the entire frame has been validated.
telemetry_result_t telemetry_frame_decode(const uint8_t* frame, size_t frame_length,
                                          vehicle_state_t* packet);

// CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, xorout=0x0000, refin=false.
uint16_t telemetry_crc16_ccitt_false(const uint8_t* data, size_t length);

const char* telemetry_result_name(telemetry_result_t result);

#ifdef __cplusplus
}
#endif
