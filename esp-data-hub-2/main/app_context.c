#include "app_context.h"

#include <string.h>

void app_context_deinit(app_context_t* ctx) {
  if (ctx == NULL) {
    return;
  }

  if (ctx->can_rx_queue != NULL) {
    vQueueDelete(ctx->can_rx_queue);
    ctx->can_rx_queue = NULL;
  }
  if (ctx->ecu_can_frames != NULL) {
    vQueueDelete(ctx->ecu_can_frames);
    ctx->ecu_can_frames = NULL;
  }
  if (ctx->vdc_can_frames != NULL) {
    vQueueDelete(ctx->vdc_can_frames);
    ctx->vdc_can_frames = NULL;
  }
  if (ctx->display_state_mutex != NULL) {
    vSemaphoreDelete(ctx->display_state_mutex);
    ctx->display_state_mutex = NULL;
  }
  if (ctx->bt_state_mutex != NULL) {
    vSemaphoreDelete(ctx->bt_state_mutex);
    ctx->bt_state_mutex = NULL;
  }
}

bool app_context_init(app_context_t* ctx, twai_node_handle_t node_hdl) {
  if (ctx == NULL) {
    return false;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->node_hdl = node_hdl;

  ctx->can_rx_queue = xQueueCreate(16, sizeof(can_rx_frame_t));
  ctx->ecu_can_frames = xQueueCreate(16, sizeof(can_rx_frame_t));
  ctx->vdc_can_frames = xQueueCreate(16, sizeof(can_rx_frame_t));
  ctx->display_state_mutex = xSemaphoreCreateMutex();
  ctx->bt_state_mutex = xSemaphoreCreateMutex();

  if (ctx->can_rx_queue == NULL || ctx->ecu_can_frames == NULL || ctx->vdc_can_frames == NULL ||
      ctx->display_state_mutex == NULL || ctx->bt_state_mutex == NULL) {
    app_context_deinit(ctx);
    return false;
  }

  return true;
}
