#include "request_vdc.h"

#include <string.h>

#include "can_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "isotp.h"

size_t request_vdc_build_payload(uint8_t* out_payload, size_t out_capacity) {
  if (out_payload == NULL) {
    return 0;
  }

  // clang-format off
  static const uint8_t payload[] = {
      0x22,  // read memory by addr list
      0x10, 0x10, // brake pressure
      0x10, 0x29, // steering angle
  };
  // clang-format on

  if (out_capacity < sizeof(payload)) {
    return 0;
  }

  memcpy(out_payload, payload, sizeof(payload));
  return sizeof(payload);
}

void request_vdc_send(twai_node_handle_t node_hdl) {
  uint8_t uds_req_payload[8] = {0};
  size_t uds_len = request_vdc_build_payload(uds_req_payload, sizeof(uds_req_payload));
  if (uds_len == 0) {
    return;
  }

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
