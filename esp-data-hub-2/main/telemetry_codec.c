#include "telemetry_codec.h"

#include <stdbool.h>

#include "cbor.h"
#include "esp_log.h"

static const char* TAG = "telemetry_codec";

void telemetry_codec_debug_log_frame(bool is_rx, const uint8_t* buffer, size_t length) {
  static const char hex_chars[] = "0123456789ABCDEF";
  const size_t max_len = (length > 64) ? 64 : length;
  char hex_out[3 * 64 + 1];
  size_t out_idx = 0;

  for (size_t i = 0; i < max_len; i++) {
    uint8_t byte = buffer[i];
    hex_out[out_idx++] = hex_chars[byte >> 4];
    hex_out[out_idx++] = hex_chars[byte & 0x0F];
    if (i + 1 < max_len) {
      hex_out[out_idx++] = ' ';
    }
  }
  hex_out[out_idx] = '\0';

  ESP_LOGI(TAG, "%s %s", is_rx ? "[RX]" : "[TX]", hex_out);
}

void telemetry_codec_encode_packet(const telemetry_packet_t* packet, uint8_t* buffer, size_t buffer_size,
                                   size_t* encoded_size) {
  CborEncoder encoder, array_encoder;
  cbor_encoder_init(&encoder, buffer, buffer_size, 0);

  CborError err = cbor_encoder_create_array(&encoder, &array_encoder, 14);
  err |= cbor_encode_uint(&array_encoder, packet->sequence);
  err |= cbor_encode_uint(&array_encoder, packet->timestamp_ms);

  err |= cbor_encode_float(&array_encoder, packet->water_temp);
  err |= cbor_encode_float(&array_encoder, packet->oil_temp);
  err |= cbor_encode_float(&array_encoder, packet->oil_pressure);

  err |= cbor_encode_float(&array_encoder, packet->dam);
  err |= cbor_encode_float(&array_encoder, packet->af_learned);
  err |= cbor_encode_float(&array_encoder, packet->af_ratio);
  err |= cbor_encode_float(&array_encoder, packet->int_temp);

  err |= cbor_encode_float(&array_encoder, packet->fb_knock);
  err |= cbor_encode_float(&array_encoder, packet->af_correct);
  err |= cbor_encode_float(&array_encoder, packet->inj_duty);
  err |= cbor_encode_float(&array_encoder, packet->eth_conc);

  err |= cbor_encode_float(&array_encoder, packet->engine_rpm);
  err |= cbor_encoder_close_container(&encoder, &array_encoder);

  if (err == CborNoError) {
    *encoded_size = cbor_encoder_get_buffer_size(&encoder, buffer);
  } else {
    *encoded_size = 0;
  }
}
