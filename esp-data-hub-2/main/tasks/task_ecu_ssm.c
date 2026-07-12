#include "task_ecu_ssm.h"

#include <stddef.h>
#include <stdint.h>

#include "app_context.h"
#include "can_transport.h"
#include "can_types.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "isotp.h"
#include "isotp_response.h"
#include "request_ecu.h"
#include "sdkconfig.h"

static const char* TAG = "task_ecu_ssm";

static void apply_ecu_response(const request_ecu_response_t* response, vehicle_state_t* state) {
  state->water_temp = response->water_temp;
  state->af_correct = response->af_correct;
  state->af_learned = response->af_learned;
  state->engine_rpm = response->engine_rpm;
  state->int_temp = response->int_temp;
  state->af_ratio = response->af_ratio;
  state->dam = response->dam;
  state->fb_knock = response->fb_knock;
  state->throttle_pos = response->throttle_pos;
  state->inj_duty = response->inj_duty;
  state->eth_conc = response->eth_conc;
}

void task_ecu_ssm(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL || app->node_hdl == NULL) {
    vTaskDelete(NULL);
    return;
  }

  const TickType_t poll_period_ticks =
      pdMS_TO_TICKS(CONFIG_DH_ECU_POLL_PERIOD_MS > 0 ? CONFIG_DH_ECU_POLL_PERIOD_MS : 1);
  TickType_t last_wake = xTaskGetTickCount();

  while (1) {
    vTaskDelayUntil(&last_wake, poll_period_ticks);

    can_rx_frame_t stale;
    while (xQueueReceive(app->ecu_can_frames, &stale, 0) == pdTRUE) {
      ESP_LOGW(TAG, "Drained stale ECU frame ID 0x%0X", stale.id);
    }

    uint8_t ssm_req_payload[64] = {0};
    const size_t payload_len = request_ecu_build_poll_payload(ssm_req_payload, sizeof(ssm_req_payload));
    if (payload_len == 0) {
      ESP_LOGE(TAG, "Failed to build ECU request payload");
      continue;
    }

    uint8_t can_frames[16][8] = {0};
    size_t frame_count = 0;
    if (!isotp_wrap_payload(ssm_req_payload, (uint16_t)payload_len, can_frames, 16, &frame_count)) {
      ESP_LOGE(TAG, "Failed to build ISO-TP frames for ECU data request");
      continue;
    }
    if (!can_transport_transmit_frame(app->node_hdl, ECU_REQ_ID, can_frames[0], 8)) {
      continue;
    }

    uint8_t fc_block_size = 0;
    uint8_t fc_stmin_raw = 0;
    if (frame_count > 1 &&
        !isotp_wait_for_fc(app->ecu_can_frames, pdMS_TO_TICKS(1000), &fc_block_size, &fc_stmin_raw, TAG)) {
      continue;
    }

    bool sent_all = true;
    uint8_t frames_sent_in_block = 0;
    for (size_t i = 1; i < frame_count; i++) {
      if (!can_transport_transmit_frame(app->node_hdl, ECU_REQ_ID, can_frames[i], 8)) {
        sent_all = false;
        break;
      }
      if (CONFIG_DH_TWAI_ISOTP_CF_GAP_US > 0) {
        esp_rom_delay_us(CONFIG_DH_TWAI_ISOTP_CF_GAP_US);
      }
      frames_sent_in_block++;
      if (i + 1 < frame_count) {
        isotp_wait_stmin(fc_stmin_raw);
      }
      if (fc_block_size > 0 && frames_sent_in_block >= fc_block_size && i + 1 < frame_count) {
        if (!isotp_wait_for_fc(app->ecu_can_frames, pdMS_TO_TICKS(1000), &fc_block_size, &fc_stmin_raw, TAG)) {
          sent_all = false;
          break;
        }
        frames_sent_in_block = 0;
      }
    }
    if (!sent_all) {
      continue;
    }

    uint8_t assembled_payload[128] = {0};
    size_t assembled_len = 0;
    if (!isotp_collect_response(app->ecu_can_frames, app->node_hdl, ECU_REQ_ID, "ECU", TAG, assembled_payload,
                                sizeof(assembled_payload), &assembled_len)) {
      continue;
    }

    request_ecu_response_t response = {0};
    if (!request_ecu_parse_ssm_response(assembled_payload, assembled_len, &response)) {
      ESP_LOGW(TAG, "failed to parse SSM response len=%u sid=0x%02X", (unsigned)assembled_len,
               assembled_len > 0 ? assembled_payload[0] : 0x00);
      continue;
    }

    if (xSemaphoreTake(app->vehicle_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      apply_ecu_response(&response, &app->vehicle_state);
      xSemaphoreGive(app->vehicle_state_mutex);
    } else {
      ESP_LOGW(TAG, "failed to take vehicle_state_mutex");
    }
  }
}
