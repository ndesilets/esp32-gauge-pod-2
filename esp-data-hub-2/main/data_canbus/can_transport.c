#include "can_transport.h"

#include <string.h>

#include "can_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"

static const char* TAG = "can_transport";

#define CAN_TX_SLOT_COUNT 9

typedef struct {
  twai_frame_t frame;
  uint8_t data[8];
} can_tx_slot_t;

static can_tx_slot_t s_tx_slots[CAN_TX_SLOT_COUNT];
static QueueHandle_t s_tx_free_slots;

bool can_transport_init(void) {
  s_tx_free_slots = xQueueCreate(CAN_TX_SLOT_COUNT, sizeof(can_tx_slot_t*));
  if (s_tx_free_slots == NULL) {
    ESP_LOGE(TAG, "Failed to create TX frame pool");
    return false;
  }

  for (size_t i = 0; i < CAN_TX_SLOT_COUNT; ++i) {
    can_tx_slot_t* slot = &s_tx_slots[i];
    if (xQueueSend(s_tx_free_slots, &slot, 0) != pdTRUE) {
      ESP_LOGE(TAG, "Failed to populate TX frame pool");
      vQueueDelete(s_tx_free_slots);
      s_tx_free_slots = NULL;
      return false;
    }
  }
  return true;
}

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

bool can_transport_tx_done_callback(twai_node_handle_t handle, const twai_tx_done_event_data_t* edata, void* user_ctx) {
  (void)handle;
  (void)user_ctx;
  BaseType_t high_task_woken = pdFALSE;

  if (s_tx_free_slots == NULL || edata == NULL || edata->done_tx_frame == NULL) {
    return false;
  }

  for (size_t i = 0; i < CAN_TX_SLOT_COUNT; ++i) {
    can_tx_slot_t* slot = &s_tx_slots[i];
    if (edata->done_tx_frame == &slot->frame) {
      xQueueSendFromISR(s_tx_free_slots, &slot, &high_task_woken);
      return high_task_woken == pdTRUE;
    }
  }
  return false;
}

bool can_transport_transmit_frame(twai_node_handle_t node_hdl, uint16_t dest, const uint8_t* buffer, size_t payload_len) {
  if (s_tx_free_slots == NULL || buffer == NULL || payload_len > sizeof(s_tx_slots[0].data)) {
    ESP_LOGW(TAG, "Invalid or uninitialized TX frame request");
    return false;
  }

  can_tx_slot_t* slot = NULL;
  if (xQueueReceive(s_tx_free_slots, &slot, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "No persistent TX frame slots available");
    return false;
  }

  memcpy(slot->data, buffer, payload_len);
  slot->frame = (twai_frame_t){
      .header.id = dest,
      .header.ide = false,
      .buffer = slot->data,
      .buffer_len = payload_len,
  };
  esp_err_t err = twai_node_transmit(node_hdl, &slot->frame, 100);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "transmit to 0x%03X failed: %s", dest, esp_err_to_name(err));
    xQueueSend(s_tx_free_slots, &slot, 0);
    return false;
  }
  return true;
}
