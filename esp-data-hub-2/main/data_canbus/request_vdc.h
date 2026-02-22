#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_twai.h"

void request_vdc_send(twai_node_handle_t node_hdl);
bool request_vdc_parse_response(const uint8_t* uds_payload, size_t length, float* out_brake_pressure_bar,
                                float* out_steering_angle_deg);
