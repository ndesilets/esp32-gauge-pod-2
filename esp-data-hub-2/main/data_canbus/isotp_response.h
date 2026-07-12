#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

bool isotp_collect_response(QueueHandle_t rx_queue, twai_node_handle_t node_hdl, uint32_t fc_dest_id,
                            const char* label, const char* log_tag, uint8_t* out_payload,
                            size_t out_payload_cap, size_t* out_payload_len);
