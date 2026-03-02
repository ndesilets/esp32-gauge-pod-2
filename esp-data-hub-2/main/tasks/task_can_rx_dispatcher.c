#include "task_can_rx_dispatcher.h"

#include "app_context.h"
#include "can_transport.h"
#include "esp_log.h"

static const char* TAG = "task_can_rx_dispatcher";

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

void task_can_rx_dispatcher(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL) {
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    can_rx_frame_t frame;
    if (xQueueReceive(app->can_rx_queue, &frame, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    can_transport_dispatch_can_frame(app, &frame);

    // drain any backlog without blocking
    while (xQueueReceive(app->can_rx_queue, &frame, 0) == pdTRUE) {
      can_transport_dispatch_can_frame(app, &frame);
    }
  }
}
