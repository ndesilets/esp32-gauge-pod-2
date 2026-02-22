#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_context.h"
#include "esp_twai.h"

bool can_transport_rx_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx);
void can_transport_transmit_frame(twai_node_handle_t node_hdl, uint16_t dest, uint8_t* buffer, size_t payload_len);
void can_transport_dispatch_can_frame(app_context_t* app, const can_rx_frame_t* frame);
