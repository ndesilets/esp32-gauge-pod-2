#include "isotp_response.h"

#include "can_types.h"
#include "esp_log.h"
#include "isotp.h"

bool isotp_collect_response(QueueHandle_t rx_queue, twai_node_handle_t node_hdl, uint32_t fc_dest_id,
                            const char* label, const char* log_tag, uint8_t* out_payload,
                            size_t out_payload_cap, size_t* out_payload_len) {
  if (rx_queue == NULL || node_hdl == NULL || label == NULL || log_tag == NULL || out_payload == NULL ||
      out_payload_len == NULL) {
    return false;
  }

  can_rx_frame_t first = {0};
  bool got_first = false;
  while (1) {
    if (xQueueReceive(rx_queue, &first, pdMS_TO_TICKS(200)) != pdTRUE) {
      ESP_LOGE(log_tag, "Didn't receive response from %s", label);
      break;
    }
    if ((first.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
      ESP_LOGW(log_tag, "Skipping FC frame while waiting for %s response", label);
      continue;
    }
    got_first = true;
    break;
  }
  if (!got_first) {
    return false;
  }

  if ((first.data[0] & 0xF0) == ISOTP_FIRST_FRAME && !isotp_send_flow_control(node_hdl, fc_dest_id)) {
    return false;
  }

  if ((first.data[0] & 0xF0) == ISOTP_SINGLE_FRAME) {
    if (!isotp_unwrap_frames(&first, 1, out_payload, out_payload_cap, out_payload_len)) {
      ESP_LOGE(log_tag, "Failed to unwrap ISO-TP single frame from %s", label);
      return false;
    }
    return true;
  }

  const size_t expected_len = ((first.data[0] & 0x0F) << 8) | first.data[1];
  can_rx_frame_t frames[16] = {0};
  uint8_t frame_idx = 0;
  frames[frame_idx++] = first;

  const size_t remaining = (expected_len > 6) ? (expected_len - 6) : 0;
  const uint8_t expected_cfs = (uint8_t)((remaining + 6) / 7);
  const uint8_t expected_frames = (uint8_t)(1 + expected_cfs);

  while (frame_idx < expected_frames && frame_idx < 16) {
    can_rx_frame_t frame = {0};
    if (xQueueReceive(rx_queue, &frame, pdMS_TO_TICKS(200)) != pdTRUE) {
      ESP_LOGE(log_tag, "Timeout waiting for %s response frames (%u/%u)", label, (unsigned)frame_idx,
               (unsigned)expected_frames);
      break;
    }
    if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
      ESP_LOGW(log_tag, "Skipping FC frame during %s response collection", label);
      continue;
    }
    frames[frame_idx++] = frame;
  }

  if (!isotp_unwrap_frames(frames, frame_idx, out_payload, out_payload_cap, out_payload_len)) {
    ESP_LOGE(log_tag, "Failed to unwrap ISO-TP frames from %s", label);
    return false;
  }
  return true;
}
