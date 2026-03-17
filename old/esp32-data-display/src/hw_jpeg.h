#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "driver/jpeg_decode.h"
}

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool hw_jpeg_init(size_t max_jpeg_size_bytes, uint32_t max_width,
                  uint32_t max_height);

bool hw_jpeg_decode_file_to_lvimg(const char* path, lv_image_dsc_t* out_dsc);

void hw_jpeg_deinit(void);

#ifdef __cplusplus
}
#endif