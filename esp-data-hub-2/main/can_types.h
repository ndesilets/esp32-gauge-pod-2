#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ECU_REQ_ID 0x7E0
#define ECU_RES_ID 0x7E8
#define VDC_REQ_ID 0x7B0
#define VDC_RES_ID 0x7B8

typedef struct {
  uint32_t id;
  bool ide;
  uint8_t data[8];
  uint8_t data_len;
} can_rx_frame_t;
