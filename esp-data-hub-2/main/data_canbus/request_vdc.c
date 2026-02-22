#include "request_vdc.h"

#include <string.h>

#include "can_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "isotp.h"

void request_vdc_send(twai_node_handle_t node_hdl) {
  uint8_t uds_req_payload[8] = {0};
  const size_t uds_len = 5;
  uds_req_payload[0] = 0x22;  // read data by identifier
  uds_req_payload[1] = 0x10;  // brake pressure DID 0x102B
  uds_req_payload[2] = 0x2B;
  uds_req_payload[3] = 0x10;  // steering angle DID 0x1029
  uds_req_payload[4] = 0x29;

  uint8_t can_frame[8] = {0};
  can_frame[0] = ISOTP_SINGLE_FRAME | (uint8_t)(uds_len & 0x0F);
  memcpy(&can_frame[1], &uds_req_payload[0], uds_len);

  twai_frame_t msg = {
      .header.id = VDC_REQ_ID,
      .header.ide = false,
      .buffer = can_frame,
      .buffer_len = 8,
  };
  ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &msg, pdMS_TO_TICKS(1000)));
}

bool request_vdc_parse_response(const uint8_t* uds_payload, size_t length, float* out_brake_pressure_bar,
                                float* out_steering_angle_deg) {
  if (uds_payload == NULL || out_brake_pressure_bar == NULL || out_steering_angle_deg == NULL) {
    return false;
  }
  // Expected layout:
  // [0] 0x62, then two entries of [DID_H][DID_L][VALUE_H][VALUE_L]
  if (length < 9 || uds_payload[0] != 0x62) {
    return false;
  }

  bool got_brake = false;
  bool got_steering = false;
  for (size_t i = 1; i + 3 < length; i += 4) {
    const uint16_t did = (uint16_t)((uds_payload[i] << 8) | uds_payload[i + 1]);
    const uint16_t raw = (uint16_t)((uds_payload[i + 2] << 8) | uds_payload[i + 3]);
    if (did == 0x102B) {
      *out_brake_pressure_bar = (float)raw;
      got_brake = true;
    } else if (did == 0x1029) {
      *out_steering_angle_deg = (float)(-((int16_t)raw));
      got_steering = true;
    }
  }
  return got_brake && got_steering;
}
