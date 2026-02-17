#include "ecu_request.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

static const char* TAG = "isotp";

void isotp_init(isotp_context_t* ctx) {
  if (!ctx) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
}

static isotp_buffer_entry_t* find_slot(isotp_context_t* ctx, uint32_t can_id, bool allow_new) {
  isotp_buffer_entry_t* free_slot = NULL;

  for (size_t i = 0; i < ISOTP_BUFFER_SLOTS; ++i) {
    isotp_buffer_entry_t* slot = &ctx->slots[i];
    if (slot->in_use && slot->can_id == can_id) {
      return slot;
    }
    if (!slot->in_use && free_slot == NULL) {
      free_slot = slot;
    }
  }

  if (allow_new && free_slot) {
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = true;
    free_slot->can_id = can_id;
    return free_slot;
  }

  return NULL;
}

static bool copy_payload(uint8_t* dst, size_t dst_cap, size_t* dst_len, const uint8_t* src, size_t src_len) {
  if (*dst_len + src_len > dst_cap) {
    return false;
  }

  memcpy(dst + *dst_len, src, src_len);
  *dst_len += src_len;
  return true;
}

bool isotp_parse_frame(isotp_context_t* ctx, const can_raw_msg_t* raw, isotp_processed_msg_t* out) {
  if (!ctx || !raw || !out || !raw->payload || raw->payload_len == 0) {
    return false;
  }

  const uint8_t pci_byte = raw->payload[0];
  const uint8_t pci_high = (pci_byte & 0xF0u) >> 4;
  const uint8_t pci_low = pci_byte & 0x0Fu;

  switch (pci_high) {
    case ISOTP_FRAME_SINGLE: {
      size_t data_len = pci_low;
      if (data_len > raw->payload_len - 1) {
        ESP_LOGW(TAG, "Single frame length %zu exceeds payload size %zu", data_len, raw->payload_len);
        return false;
      }
      if (data_len > ISOTP_MAX_MESSAGE_BYTES) {
        ESP_LOGW(TAG, "Single frame length %zu exceeds buffer (%d)", data_len, ISOTP_MAX_MESSAGE_BYTES);
        return false;
      }

      out->timestamp_ms = raw->timestamp_ms;
      out->can_id = raw->can_id;
      out->payload_len = data_len;
      memcpy(out->payload, raw->payload + 1, data_len);
      return true;
    }

    case ISOTP_FRAME_FIRST: {
      if (raw->payload_len < 2) {
        ESP_LOGW(TAG, "First frame too short");
        return false;
      }
      uint16_t data_length = ((uint16_t)pci_low << 8) | raw->payload[1];
      if (data_length > ISOTP_MAX_MESSAGE_BYTES) {
        ESP_LOGW(TAG, "First frame length %u exceeds buffer (%d)", data_length, ISOTP_MAX_MESSAGE_BYTES);
        return false;
      }

      isotp_buffer_entry_t* slot = find_slot(ctx, raw->can_id, true);
      if (!slot) {
        ESP_LOGW(TAG, "No free ISO-TP buffer slot for CAN ID 0x%08" PRIx32, raw->can_id);
        return false;
      }

      slot->timestamp_ms = raw->timestamp_ms;
      slot->expected_len = data_length;
      slot->last_seq = 0;
      slot->payload_len = 0;

      const uint8_t* frame_payload = raw->payload_len > 2 ? raw->payload + 2 : NULL;
      size_t frame_len = raw->payload_len > 2 ? raw->payload_len - 2 : 0;
      if (frame_len > 0 && !copy_payload(slot->payload, ISOTP_MAX_MESSAGE_BYTES, &slot->payload_len,
                                         frame_payload, frame_len)) {
        ESP_LOGW(TAG, "First frame payload overflow for CAN ID 0x%08" PRIx32, raw->can_id);
        slot->in_use = false;
        return false;
      }
      return false;
    }

    case ISOTP_FRAME_CONSECUTIVE: {
      isotp_buffer_entry_t* slot = find_slot(ctx, raw->can_id, false);
      if (!slot) {
        // Trace likely started mid-transfer; drop the frame.
        return false;
      }

      const uint8_t seq_num = pci_low;
      // Optional: verify sequence numbers if needed.
      slot->last_seq = seq_num;

      const uint8_t* frame_payload = raw->payload_len > 1 ? raw->payload + 1 : NULL;
      size_t frame_len = raw->payload_len > 1 ? raw->payload_len - 1 : 0;
      if (frame_len > 0 && !copy_payload(slot->payload, ISOTP_MAX_MESSAGE_BYTES, &slot->payload_len,
                                         frame_payload, frame_len)) {
        ESP_LOGW(TAG, "Consecutive frame overflow for CAN ID 0x%08" PRIx32, raw->can_id);
        slot->in_use = false;
        return false;
      }

      if (slot->payload_len >= slot->expected_len) {
        out->timestamp_ms = slot->timestamp_ms;
        out->can_id = raw->can_id;
        out->payload_len = slot->expected_len;
        memcpy(out->payload, slot->payload, slot->expected_len);

        slot->in_use = false;
        return true;
      }
      return false;
    }

    case ISOTP_FRAME_FLOW_CONTROL:
    default:
      // Flow control or unknown frame types are ignored.
      return false;
  }
}
