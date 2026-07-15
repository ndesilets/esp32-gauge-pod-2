#include "isotp.h"

#include "can_transport.h"
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

bool isotp_send_flow_control(twai_node_handle_t node_hdl, uint32_t to) {
  uint8_t fc_data[3] = {ISOTP_FLOW_CONTROL_FRAME, 0, 0};  // let 'er eat bud
  return can_transport_transmit_frame(node_hdl, to, fc_data, sizeof(fc_data));
}
