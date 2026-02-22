#include "can_transport.h"

#include <string.h>

#include "can_types.h"
#include "esp_err.h"
#include "esp_log.h"

static const char* TAG = "can_transport";

bool can_transport_rx_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx) {
  (void)edata;
  app_context_t* app = (app_context_t*)user_ctx;
  BaseType_t high_task_woken = pdFALSE;

  if (app == NULL || app->can_rx_queue == NULL) {
    return false;
  }

  uint8_t rx_buf[8] = {0};
  twai_frame_t rx_frame = {
      .buffer = rx_buf,
      .buffer_len = sizeof(rx_buf),
  };
  if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
    // TODO some mechanism of reporting errors/dropped frames
    return false;
  }

  if (rx_frame.header.id != ECU_RES_ID && rx_frame.header.id != VDC_RES_ID) {
    // not an id we care about
    return false;
  }

  can_rx_frame_t out = {
      .id = rx_frame.header.id,
      .ide = rx_frame.header.ide,
      .data_len = rx_frame.header.dlc,
  };
  memcpy(out.data, rx_buf, out.data_len);
  xQueueSendFromISR(app->can_rx_queue, &out, &high_task_woken);

  return (high_task_woken == pdTRUE);
}

void can_transport_transmit_frame(twai_node_handle_t node_hdl, uint16_t dest, uint8_t* buffer, size_t payload_len) {
  twai_frame_t frame = {
      .header.id = dest,
      .header.ide = false,
      .buffer = buffer,
      .buffer_len = payload_len,
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &frame, pdMS_TO_TICKS(100)));
}

void can_transport_dispatch_can_frame(app_context_t* app, const can_rx_frame_t* frame) {
  if (app == NULL || frame == NULL) {
    return;
  }

  switch (frame->id) {
    case ECU_RES_ID:
      xQueueSend(app->ecu_can_frames, frame, pdMS_TO_TICKS(10));
      break;
    case VDC_RES_ID:
      xQueueSend(app->vdc_can_frames, frame, pdMS_TO_TICKS(10));
      break;
    default:
      ESP_LOGW(TAG, "Unhandled CAN frame ID: 0x%03X", frame->id);
      break;
  }
}
