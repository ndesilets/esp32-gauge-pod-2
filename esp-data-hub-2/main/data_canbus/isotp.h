#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_types.h"
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ISOTP_FC_BS_LET_ER_EAT_BUD 0x00
#define ISOTP_FC_BS_CONTINUE 0x01
#define ISOTP_FC_BS_WAIT 0x02
#define ISOTP_FC_BS_ABORT 0x03

#define ISOTP_SINGLE_FRAME 0x00
#define ISOTP_FIRST_FRAME 0x10
#define ISOTP_CONSECUTIVE_FRAME 0x20
#define ISOTP_FLOW_CONTROL_FRAME 0x30

void isotp_wait_stmin(uint8_t stmin_raw);
bool isotp_wait_for_fc(QueueHandle_t queue, TickType_t timeout, uint8_t* out_bs, uint8_t* out_stmin, const char* tag);
void isotp_send_flow_control(twai_node_handle_t node_hdl, uint32_t to);
bool isotp_wrap_payload(const uint8_t* payload, uint16_t payload_len, uint8_t frames[][8], size_t max_frames,
                        size_t* out_frame_count);
bool isotp_unwrap_frames(const can_rx_frame_t frames[16], size_t frame_count, uint8_t* out_payload,
                         size_t out_payload_size, size_t* out_payload_len);
