#include "task_can_rx_dispatcher.h"

#include "app_context.h"
#include "can_transport.h"

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
    while (xQueueReceive(app->can_rx_queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
      can_transport_dispatch_can_frame(app, &frame);
    }
  }
}
