#include "telemetry_protocol.h"

#include <string.h>

#include "cobs.h"
#include "mpack.h"

static telemetry_result_t encode_msgpack(const vehicle_state_t* packet, uint8_t* output,
                                         size_t output_capacity, size_t* output_length) {
  mpack_writer_t writer;
  mpack_writer_init(&writer, (char*)output, output_capacity);

  mpack_start_array(&writer, TELEMETRY_MSGPACK_ITEM_COUNT);
  mpack_write_u32(&writer, TELEMETRY_SCHEMA_VERSION);
  mpack_write_u32(&writer, packet->sequence);
  mpack_write_u32(&writer, packet->timestamp_ms);
  mpack_write_float(&writer, packet->water_temp);
  mpack_write_float(&writer, packet->oil_temp);
  mpack_write_float(&writer, packet->oil_pressure);
  mpack_write_float(&writer, packet->dam);
  mpack_write_float(&writer, packet->af_learned);
  mpack_write_float(&writer, packet->af_ratio);
  mpack_write_float(&writer, packet->int_temp);
  mpack_write_float(&writer, packet->fb_knock);
  mpack_write_float(&writer, packet->af_correct);
  mpack_write_float(&writer, packet->inj_duty);
  mpack_write_float(&writer, packet->eth_conc);
  mpack_write_float(&writer, packet->engine_rpm);
  mpack_write_float(&writer, packet->throttle_pos);
  mpack_write_float(&writer, packet->brake_pressure_bar);
  mpack_write_float(&writer, packet->steering_angle_deg);
  mpack_finish_array(&writer);

  const size_t bytes_written = mpack_writer_buffer_used(&writer);
  if (mpack_writer_destroy(&writer) != mpack_ok) {
    return TELEMETRY_RESULT_OUTPUT_TOO_SMALL;
  }

  *output_length = bytes_written;
  return TELEMETRY_RESULT_OK;
}

static telemetry_result_t decode_msgpack(const uint8_t* payload, size_t payload_length,
                                         vehicle_state_t* packet) {
  mpack_reader_t reader;
  mpack_reader_init_data(&reader, (const char*)payload, payload_length);

  mpack_expect_array_match(&reader, TELEMETRY_MSGPACK_ITEM_COUNT);
  const uint32_t schema_version = mpack_expect_u32(&reader);

  vehicle_state_t decoded = {0};
  decoded.sequence = mpack_expect_u32(&reader);
  decoded.timestamp_ms = mpack_expect_u32(&reader);
  decoded.water_temp = mpack_expect_float_strict(&reader);
  decoded.oil_temp = mpack_expect_float_strict(&reader);
  decoded.oil_pressure = mpack_expect_float_strict(&reader);
  decoded.dam = mpack_expect_float_strict(&reader);
  decoded.af_learned = mpack_expect_float_strict(&reader);
  decoded.af_ratio = mpack_expect_float_strict(&reader);
  decoded.int_temp = mpack_expect_float_strict(&reader);
  decoded.fb_knock = mpack_expect_float_strict(&reader);
  decoded.af_correct = mpack_expect_float_strict(&reader);
  decoded.inj_duty = mpack_expect_float_strict(&reader);
  decoded.eth_conc = mpack_expect_float_strict(&reader);
  decoded.engine_rpm = mpack_expect_float_strict(&reader);
  decoded.throttle_pos = mpack_expect_float_strict(&reader);
  decoded.brake_pressure_bar = mpack_expect_float_strict(&reader);
  decoded.steering_angle_deg = mpack_expect_float_strict(&reader);
  mpack_done_array(&reader);

  const size_t trailing_bytes = mpack_reader_remaining(&reader, NULL);
  const mpack_error_t error = mpack_reader_destroy(&reader);
  if (error != mpack_ok || trailing_bytes != 0) {
    return TELEMETRY_RESULT_MSGPACK_ERROR;
  }
  if (schema_version != TELEMETRY_SCHEMA_VERSION) {
    return TELEMETRY_RESULT_SCHEMA_ERROR;
  }

  *packet = decoded;
  return TELEMETRY_RESULT_OK;
}

uint16_t telemetry_crc16_ccitt_false(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

telemetry_result_t telemetry_frame_encode(const vehicle_state_t* packet, uint8_t* output,
                                          size_t output_capacity, size_t* output_length) {
  if (packet == NULL || output == NULL || output_length == NULL) {
    return TELEMETRY_RESULT_INVALID_ARGUMENT;
  }
  *output_length = 0;
  if (output_capacity < TELEMETRY_COBS_FRAME_MAX_SIZE) {
    return TELEMETRY_RESULT_OUTPUT_TOO_SMALL;
  }

  uint8_t raw_frame[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t payload_length = 0;
  telemetry_result_t result =
      encode_msgpack(packet, raw_frame, TELEMETRY_MSGPACK_MAX_SIZE, &payload_length);
  if (result != TELEMETRY_RESULT_OK) {
    return result;
  }

  const uint16_t crc = telemetry_crc16_ccitt_false(raw_frame, payload_length);
  raw_frame[payload_length] = (uint8_t)(crc >> 8);
  raw_frame[payload_length + 1] = (uint8_t)crc;

  *output_length = cobs_encode(raw_frame, payload_length + 2, output);
  return TELEMETRY_RESULT_OK;
}

telemetry_result_t telemetry_frame_decode(const uint8_t* frame, size_t frame_length,
                                          vehicle_state_t* packet) {
  if (frame == NULL || packet == NULL || frame_length == 0) {
    return TELEMETRY_RESULT_INVALID_ARGUMENT;
  }
  if (frame_length > TELEMETRY_COBS_FRAME_MAX_SIZE) {
    return TELEMETRY_RESULT_FRAME_TOO_LARGE;
  }

  uint8_t raw_frame[TELEMETRY_RAW_FRAME_MAX_SIZE];
  size_t raw_length = 0;
  if (!cobs_decode(frame, frame_length, raw_frame, sizeof(raw_frame), &raw_length)) {
    return TELEMETRY_RESULT_COBS_ERROR;
  }
  if (raw_length < 3) {
    return TELEMETRY_RESULT_MSGPACK_ERROR;
  }

  const size_t payload_length = raw_length - 2;
  const uint16_t received_crc =
      (uint16_t)(((uint16_t)raw_frame[payload_length] << 8) | raw_frame[payload_length + 1]);
  if (telemetry_crc16_ccitt_false(raw_frame, payload_length) != received_crc) {
    return TELEMETRY_RESULT_CRC_ERROR;
  }

  return decode_msgpack(raw_frame, payload_length, packet);
}

const char* telemetry_result_name(telemetry_result_t result) {
  switch (result) {
    case TELEMETRY_RESULT_OK:
      return "ok";
    case TELEMETRY_RESULT_INVALID_ARGUMENT:
      return "invalid argument";
    case TELEMETRY_RESULT_OUTPUT_TOO_SMALL:
      return "output too small";
    case TELEMETRY_RESULT_FRAME_TOO_LARGE:
      return "frame too large";
    case TELEMETRY_RESULT_COBS_ERROR:
      return "COBS error";
    case TELEMETRY_RESULT_CRC_ERROR:
      return "CRC error";
    case TELEMETRY_RESULT_MSGPACK_ERROR:
      return "MessagePack error";
    case TELEMETRY_RESULT_SCHEMA_ERROR:
      return "schema error";
    default:
      return "unknown error";
  }
}
