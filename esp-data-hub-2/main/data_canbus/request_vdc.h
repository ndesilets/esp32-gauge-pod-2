#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_twai.h"

size_t request_vdc_build_payload(uint8_t* out_payload, size_t out_capacity);
void request_vdc_send(twai_node_handle_t node_hdl);
