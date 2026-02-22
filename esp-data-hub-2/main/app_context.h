#pragma once

#include "can_types.h"
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "telemetry_types.h"

typedef struct {
  twai_node_handle_t node_hdl;
  display_packet_t display_state;
  SemaphoreHandle_t display_state_mutex;
  bt_packet_t bt_state;
  SemaphoreHandle_t bt_state_mutex;
  QueueHandle_t can_rx_queue;
  QueueHandle_t ecu_can_frames;
  QueueHandle_t vdc_can_frames;
} app_context_t;

bool app_context_init(app_context_t* ctx, twai_node_handle_t node_hdl);
void app_context_deinit(app_context_t* ctx);
