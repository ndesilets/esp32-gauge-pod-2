#include "isotp_codec.h"

#include <string.h>

#define ISOTP_CLASSIC_SINGLE_FRAME_MAX_PAYLOAD 7U
#define ISOTP_FIRST_FRAME_PAYLOAD_SIZE 6U
#define ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE 7U
#define ISOTP_CLASSIC_MAX_PAYLOAD 4095U

bool isotp_wrap_payload(const uint8_t* payload, uint16_t payload_len, uint8_t frames[][8], size_t max_frames,
                        size_t* out_frame_count) {
  if (!payload || !frames || !out_frame_count || max_frames == 0) {
    return false;
  }

  *out_frame_count = 0;

  if (payload_len <= ISOTP_CLASSIC_SINGLE_FRAME_MAX_PAYLOAD) {
    frames[0][0] = ISOTP_SINGLE_FRAME | (uint8_t)(payload_len & 0x0F);
    memcpy(&frames[0][1], payload, payload_len);
    *out_frame_count = 1;
    return true;
  }

  if (payload_len > ISOTP_CLASSIC_MAX_PAYLOAD) {
    return false;
  }

  frames[0][0] = ISOTP_FIRST_FRAME | (uint8_t)((payload_len >> 8) & 0x0F);
  frames[0][1] = (uint8_t)(payload_len & 0xFF);
  memcpy(&frames[0][2], payload, ISOTP_FIRST_FRAME_PAYLOAD_SIZE);

  size_t offset = ISOTP_FIRST_FRAME_PAYLOAD_SIZE;
  size_t frame_idx = 1;
  uint8_t seq = 1;
  while (offset < payload_len && frame_idx < max_frames) {
    const size_t remaining = payload_len - offset;
    const size_t chunk =
        (remaining > ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE) ? ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE : remaining;

    frames[frame_idx][0] = ISOTP_CONSECUTIVE_FRAME | (seq & 0x0F);
    memcpy(&frames[frame_idx][1], payload + offset, chunk);

    offset += chunk;
    frame_idx++;
    seq = (seq + 1) & 0x0F;
  }

  *out_frame_count = frame_idx;
  return (offset >= payload_len);
}

bool isotp_unwrap_frames(const can_rx_frame_t frames[], size_t frame_count, uint8_t* out_payload,
                         size_t out_payload_size, size_t* out_payload_len) {
  if (!frames || !out_payload || !out_payload_len || frame_count == 0 || frames[0].data_len == 0) {
    return false;
  }

  const uint8_t pci = frames[0].data[0];
  const uint8_t type = pci & 0xF0;

  if (type == ISOTP_SINGLE_FRAME) {
    const uint8_t payload_len = pci & 0x0F;
    if (payload_len > ISOTP_CLASSIC_SINGLE_FRAME_MAX_PAYLOAD || out_payload_size < payload_len ||
        frames[0].data_len < (size_t)payload_len + 1U) {
      return false;
    }
    memcpy(out_payload, &frames[0].data[1], payload_len);
    *out_payload_len = payload_len;
    return true;
  }

  if (type == ISOTP_FIRST_FRAME) {
    const uint16_t payload_len = (uint16_t)(((uint16_t)(pci & 0x0F) << 8) | frames[0].data[1]);
    if (payload_len <= ISOTP_CLASSIC_SINGLE_FRAME_MAX_PAYLOAD || out_payload_size < payload_len ||
        frames[0].data_len < 8U) {
      return false;
    }

    size_t offset = 0;
    memcpy(out_payload, &frames[0].data[2], ISOTP_FIRST_FRAME_PAYLOAD_SIZE);
    offset += ISOTP_FIRST_FRAME_PAYLOAD_SIZE;

    uint8_t expected_seq = 1;
    for (size_t i = 1; i < frame_count && offset < payload_len; i++) {
      if (frames[i].data_len == 0) {
        return false;
      }

      const uint8_t cf_pci = frames[i].data[0];
      if ((cf_pci & 0xF0) != ISOTP_CONSECUTIVE_FRAME) {
        return false;
      }
      const uint8_t seq = cf_pci & 0x0F;
      if (seq != expected_seq) {
        return false;
      }

      const size_t remaining = payload_len - offset;
      const size_t chunk =
          (remaining > ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE) ? ISOTP_CONSECUTIVE_FRAME_PAYLOAD_SIZE : remaining;
      if (frames[i].data_len < chunk + 1U) {
        return false;
      }
      memcpy(out_payload + offset, &frames[i].data[1], chunk);
      offset += chunk;

      expected_seq = (expected_seq + 1) & 0x0F;
    }

    if (offset != payload_len) {
      return false;
    }

    *out_payload_len = payload_len;
    return true;
  }

  return false;
}
