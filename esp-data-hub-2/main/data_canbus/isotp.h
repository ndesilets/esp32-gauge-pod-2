#pragma once

#include <stdint.h>

#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "isotp_codec.h"

#define ISOTP_FC_BS_LET_ER_EAT_BUD 0x00
#define ISOTP_FC_BS_CONTINUE 0x01
#define ISOTP_FC_BS_WAIT 0x02
#define ISOTP_FC_BS_ABORT 0x03

void isotp_wait_stmin(uint8_t stmin_raw);
bool isotp_wait_for_fc(QueueHandle_t queue, TickType_t timeout, uint8_t* out_bs, uint8_t* out_stmin, const char* tag);
bool isotp_send_flow_control(twai_node_handle_t node_hdl, uint32_t to);
