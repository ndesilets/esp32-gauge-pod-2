#include "isotp.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/task.h"

void isotp_wait_stmin(uint8_t stmin_raw) {
  if (stmin_raw <= 0x7F) {
    if (stmin_raw == 0) {
      return;
    }

    TickType_t ticks = pdMS_TO_TICKS(stmin_raw);
    if (ticks > 0) {
      vTaskDelay(ticks);
    } else {
      esp_rom_delay_us((uint32_t)stmin_raw * 1000U);
    }
    return;
  }

  // ISO-TP sub-ms STmin encoding: 0xF1..0xF9 => 100..900us
  if (stmin_raw >= 0xF1 && stmin_raw <= 0xF9) {
    esp_rom_delay_us((uint32_t)(stmin_raw - 0xF0U) * 100U);
  }
}

bool isotp_wait_for_fc(QueueHandle_t queue, TickType_t timeout, uint8_t* out_bs, uint8_t* out_stmin, const char* tag) {
  can_rx_frame_t frame = {0};
  while (1) {
    if (xQueueReceive(queue, &frame, timeout) != pdTRUE) {
      ESP_LOGW(tag, "Didn't receive FC frame from ECU");
      return false;
    }

    if ((frame.data[0] & 0xF0) != ISOTP_FLOW_CONTROL_FRAME) {
      ESP_LOGW(tag, "Unexpected frame from ECU while waiting for FC: 0x%02X", frame.data[0]);
      continue;
    }

    uint8_t flow_status = frame.data[0] & 0x0F;
    if (flow_status == ISOTP_FC_BS_WAIT) {
      ESP_LOGW(tag, "ECU requested FC WAIT, still waiting");
      continue;
    }
    if (flow_status == ISOTP_FC_BS_ABORT) {
      ESP_LOGW(tag, "ECU requested FC ABORT");
      return false;
    }
    if (flow_status != ISOTP_FC_BS_LET_ER_EAT_BUD) {
      ESP_LOGW(tag, "Unexpected FC status from ECU: 0x%02X", flow_status);
      continue;
    }

    if (out_bs != NULL) {
      *out_bs = frame.data[1];
    }
    if (out_stmin != NULL) {
      *out_stmin = frame.data[2];
    }
    return true;
  }
}

void isotp_send_flow_control(twai_node_handle_t node_hdl, uint32_t to) {
  uint8_t fc_data[3] = {ISOTP_FLOW_CONTROL_FRAME, 0, 0};  // let 'er eat bud
  twai_frame_t fc_msg = {
      .header.id = to,
      .header.ide = false,
      .buffer = fc_data,
      .buffer_len = sizeof(fc_data),
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &fc_msg, pdMS_TO_TICKS(100)));
}

bool isotp_wrap_payload(const uint8_t* payload, uint16_t payload_len, uint8_t frames[][8], size_t max_frames,
                        size_t* out_frame_count) {
  if (!payload || !frames || !out_frame_count || max_frames == 0) {
    return false;
  }

  *out_frame_count = 0;

  if (payload_len <= 7) {
    frames[0][0] = ISOTP_SINGLE_FRAME | (uint8_t)(payload_len & 0x0F);
    memcpy(&frames[0][1], payload, payload_len);
    *out_frame_count = 1;
    return true;
  }

  frames[0][0] = ISOTP_FIRST_FRAME | (uint8_t)((payload_len >> 8) & 0x0F);
  frames[0][1] = (uint8_t)(payload_len & 0xFF);
  memcpy(&frames[0][2], payload, 6);

  size_t offset = 6;
  size_t frame_idx = 1;
  uint8_t seq = 1;
  while (offset < payload_len && frame_idx < max_frames) {
    const size_t remaining = payload_len - offset;
    const size_t chunk = (remaining > 7) ? 7 : remaining;

    frames[frame_idx][0] = ISOTP_CONSECUTIVE_FRAME | (seq & 0x0F);
    memcpy(&frames[frame_idx][1], payload + offset, chunk);

    offset += chunk;
    frame_idx++;
    seq = (seq + 1) & 0x0F;
  }

  *out_frame_count = frame_idx;
  return (offset >= payload_len);
}

bool isotp_unwrap_frames(const can_rx_frame_t frames[16], size_t frame_count, uint8_t* out_payload,
                         size_t out_payload_size, size_t* out_payload_len) {
  if (!frames || !out_payload || !out_payload_len || frame_count == 0) {
    return false;
  }

  const uint8_t pci = frames[0].data[0];
  const uint8_t type = pci & 0xF0;

  if (type == ISOTP_SINGLE_FRAME) {
    const uint8_t payload_len = pci & 0x0F;
    if (payload_len > 7 || out_payload_size < payload_len) {
      return false;
    }
    memcpy(out_payload, &frames[0].data[1], payload_len);
    *out_payload_len = payload_len;
    return true;
  }

  if (type == ISOTP_FIRST_FRAME) {
    const uint16_t payload_len = ((pci & 0x0F) << 8) | frames[0].data[1];
    if (out_payload_size < payload_len) {
      return false;
    }

    size_t offset = 0;
    memcpy(out_payload, &frames[0].data[2], 6);
    offset += 6;

    uint8_t expected_seq = 1;
    for (size_t i = 1; i < frame_count && offset < payload_len; i++) {
      const uint8_t cf_pci = frames[i].data[0];
      if ((cf_pci & 0xF0) != ISOTP_CONSECUTIVE_FRAME) {
        return false;
      }
      const uint8_t seq = cf_pci & 0x0F;
      if (seq != expected_seq) {
        return false;
      }

      const size_t remaining = payload_len - offset;
      const size_t chunk = (remaining > 7) ? 7 : remaining;
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
