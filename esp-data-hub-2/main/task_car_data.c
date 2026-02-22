#include "task_car_data.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_context.h"
#include "can_transport.h"
#include "can_types.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "isotp.h"
#include "request_ecu.h"
#include "sdkconfig.h"

static const char* TAG = "task_car_data";

void task_car_data(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL || app->node_hdl == NULL) {
    vTaskDelete(NULL);
    return;
  }

  const TickType_t poll_period_ticks =
      pdMS_TO_TICKS(CONFIG_DH_CAR_POLL_PERIOD_MS > 0 ? CONFIG_DH_CAR_POLL_PERIOD_MS : 1);

  while (1) {
    vTaskDelay(poll_period_ticks);

    // drain any stale ECU frames before starting a new request
    can_rx_frame_t stale;
    while (xQueueReceive(app->ecu_can_frames, &stale, pdMS_TO_TICKS(5)) == pdTRUE) {
      ESP_LOGW(TAG, "Drained stale ECU frame ID 0x%0X", stale.id);
    }

    // 1) build ECU payload
    uint8_t ssm_req_payload[64] = {0};
    size_t payload_len = request_ecu_build_poll_payload(ssm_req_payload, sizeof(ssm_req_payload));
    if (payload_len == 0) {
      ESP_LOGE(TAG, "Failed to build ECU request payload");
      continue;
    }

    // 2) ISO-TP wrap and transmit first frame
    uint8_t can_frames[16][8] = {0};
    size_t isotp_payload_frame_count = 0;
    if (!isotp_wrap_payload(ssm_req_payload, (uint16_t)payload_len, can_frames, 16, &isotp_payload_frame_count)) {
      ESP_LOGE(TAG, "Failed to build ISO-TP frames for ECU data request");
      continue;
    }

    can_transport_transmit_frame(app->node_hdl, ECU_REQ_ID, can_frames[0], 8);

    // 3) wait for FC if multi-frame
    uint8_t fc_block_size = 0;
    uint8_t fc_stmin_raw = 0;
    if (isotp_payload_frame_count > 1) {
      if (!isotp_wait_for_fc(app->ecu_can_frames, pdMS_TO_TICKS(1000), &fc_block_size, &fc_stmin_raw, TAG)) {
        continue;
      }
    }

    // 4) send remaining consecutive frames
    if (isotp_payload_frame_count > 1) {
      bool sent_all_cfs = true;
      uint8_t frames_sent_in_block = 0;
      for (uint8_t i = 1; i < isotp_payload_frame_count; i++) {
        can_transport_transmit_frame(app->node_hdl, ECU_REQ_ID, can_frames[i], 8);
        // This avoids ECU timing issues at max speed on some units.
        if (CONFIG_DH_TWAI_ISOTP_CF_GAP_US > 0) {
          esp_rom_delay_us(CONFIG_DH_TWAI_ISOTP_CF_GAP_US);
        }

        frames_sent_in_block++;
        if (i + 1 < isotp_payload_frame_count) {
          isotp_wait_stmin(fc_stmin_raw);
        }

        if (fc_block_size > 0 && frames_sent_in_block >= fc_block_size && i + 1 < isotp_payload_frame_count) {
          if (!isotp_wait_for_fc(app->ecu_can_frames, pdMS_TO_TICKS(1000), &fc_block_size, &fc_stmin_raw, TAG)) {
            sent_all_cfs = false;
            break;
          }
          frames_sent_in_block = 0;
        }
      }
      if (!sent_all_cfs) {
        continue;
      }
    }

    // 5) wait for first ECU response frame, skipping FC frames
    can_rx_frame_t frame = {0};
    bool got_response_frame = false;
    while (1) {
      if (xQueueReceive(app->ecu_can_frames, &frame, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "Didn't receive response from ECU");
        break;
      }
      if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
        ESP_LOGW(TAG, "Skipping FC frame while waiting for ECU response");
        continue;
      }
      got_response_frame = true;
      break;
    }
    if (!got_response_frame) {
      continue;
    }

    // 6) send FC response back to ECU if ECU started a multi-frame response
    if ((frame.data[0] & 0xF0) == ISOTP_FIRST_FRAME) {
      isotp_send_flow_control(app->node_hdl, ECU_REQ_ID);
    }

    // 7) assemble full payload
    uint8_t assembled_payload[128] = {0};
    size_t assembled_len = 0;
    if ((frame.data[0] & 0xF0) == ISOTP_SINGLE_FRAME) {
      assembled_len = frame.data[0] & 0x0F;
      memcpy(assembled_payload, &frame.data[1], assembled_len);
    } else if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
      ESP_LOGW(TAG, "Unexpected FC frame when assembling ECU response");
      continue;
    } else {
      size_t expected_len = ((frame.data[0] & 0x0F) << 8) | frame.data[1];

      can_rx_frame_t frames[16] = {0};
      uint8_t frame_idx = 0;
      frames[frame_idx++] = frame;

      size_t remaining = (expected_len > 6) ? (expected_len - 6) : 0;
      uint8_t expected_cfs = (uint8_t)((remaining + 6) / 7);
      uint8_t expected_frames = (uint8_t)(1 + expected_cfs);
      const uint8_t max_frames = 16;
      while (frame_idx < expected_frames && frame_idx < max_frames) {
        if (xQueueReceive(app->ecu_can_frames, &frame, pdMS_TO_TICKS(200)) != pdTRUE) {
          ESP_LOGE(TAG, "Timeout waiting for ECU response frames (%u/%u)", (unsigned)frame_idx,
                   (unsigned)expected_frames);
          break;
        }
        if ((frame.data[0] & 0xF0) == ISOTP_FLOW_CONTROL_FRAME) {
          ESP_LOGW(TAG, "Skipping FC frame during ECU response collection");
          continue;
        }
        frames[frame_idx++] = frame;
      }

      if (!isotp_unwrap_frames(frames, frame_idx, assembled_payload, sizeof(assembled_payload), &assembled_len)) {
        ESP_LOGE(TAG, "Failed to unwrap ISO-TP frames from ECU");
        continue;
      }
    }

    // 8) parse and apply telemetry updates
    request_ecu_response_t response;
    if (!request_ecu_parse_ssm_response(assembled_payload, assembled_len, &response)) {
      ESP_LOGW(TAG, "failed to parse SSM response len=%u sid=0x%02X", (unsigned)assembled_len,
               assembled_len > 0 ? assembled_payload[0] : 0x00);
      continue;
    }

    if (xSemaphoreTake(app->current_state_mutex, 0) == pdTRUE) {
      request_ecu_apply_to_telemetry(&response, &app->current_state);
      xSemaphoreGive(app->current_state_mutex);
    }

    // --- VDC DATA (scaffold only, intentionally not active)
    // request_vdc_send(app->node_hdl);
  }
}
