#include "hw_jpeg.h"

#include <stdio.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char* TAG = "hw_jpeg";

static jpeg_decoder_handle_t s_dec = NULL;

static uint8_t* s_bitstream = NULL;
static size_t s_bitstream_size = 0;

static uint8_t* s_outbuf = NULL;
static size_t s_outbuf_size = 0;

static uint32_t s_img_w = 0;
static uint32_t s_img_h = 0;

bool hw_jpeg_init(size_t max_jpeg_size_bytes, uint32_t max_width, uint32_t max_height) {
  // create decoder engine
  jpeg_decode_engine_cfg_t eng_cfg = {
      .intr_priority = 0,
      .timeout_ms = 1000,
  };

  if (jpeg_new_decoder_engine(&eng_cfg, &s_dec) != ESP_OK) {
    ESP_LOGE(TAG, "jpeg_new_decoder_engine failed");
    return false;
  }

  // allocate input/output buffers witih helper so they are aligned

  jpeg_decode_memory_alloc_cfg_t in_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
  };
  jpeg_decode_memory_alloc_cfg_t out_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
  };

  size_t aligned_in_size = 0;
  size_t aligned_out_size = 0;

  s_bitstream = (uint8_t*)jpeg_alloc_decoder_mem(max_jpeg_size_bytes, &in_cfg, &aligned_in_size);
  if (!s_bitstream) {
    ESP_LOGE(TAG, "jpeg_alloc_decoder_mem input failed");
    return false;
  }
  s_bitstream_size = aligned_in_size;

  // rgb565: w * h * 2
  size_t requested_out = (size_t)max_width * (size_t)max_height * 2;
  s_outbuf = (uint8_t*)jpeg_alloc_decoder_mem(requested_out, &out_cfg, &aligned_out_size);
  if (!s_outbuf) {
    ESP_LOGE(TAG, "jpeg_alloc_decoder_mem output failed");
    return false;
  }
  s_outbuf_size = aligned_out_size;

  s_img_w = max_width;
  s_img_h = max_height;

  return true;
}

bool hw_jpeg_decode_file_to_lvimg(const char* path, lv_image_dsc_t* out_dsc) {
  if (!s_dec || !s_bitstream || !s_outbuf || !out_dsc) {
    ESP_LOGE(TAG, "check hw_jpeg_decode_file_to_lvimg args");
    return false;
  }

  struct stat st = {0};
  if (stat(path, &st) != 0) {
    ESP_LOGE(TAG, "stat failed for %s", path);
    return false;
  }
  const size_t jpeg_size = st.st_size;
  if (jpeg_size == 0 || jpeg_size > s_bitstream_size) {
    ESP_LOGE(TAG, "jpeg file size invalid %u", (unsigned)jpeg_size);
    return false;
  }

  FILE* f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "fopen failed for %s", path);
    return false;
  }
  size_t read_bytes = fread(s_bitstream, 1, jpeg_size, f);
  fclose(f);

  if (read_bytes != jpeg_size) {
    ESP_LOGE(TAG, "jpeg file read size mismatch %u != %u", (unsigned)read_bytes, (unsigned)jpeg_size);
    return false;
  }

  // inspect header to get w/h

  jpeg_decode_picture_info_t pic_info;
  esp_err_t err = jpeg_decoder_get_info(s_bitstream, jpeg_size, &pic_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "jpeg_decoder_get_info failed with: %d", err);
    return false;
  }

  jpeg_decode_cfg_t dec_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
      .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
  };

  uint32_t out_size = 0;
  err = jpeg_decoder_process(s_dec, &dec_cfg, s_bitstream, jpeg_size, s_outbuf, s_outbuf_size, &out_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "jpeg_decoder_process failed with: %d", err);
    return false;
  }

  // fill lvgl image descriptor

  static lv_image_header_t header;
  header.magic = LV_IMAGE_HEADER_MAGIC;
  header.cf = LV_COLOR_FORMAT_RGB565;
  header.w = pic_info.width;
  header.h = pic_info.height;

  out_dsc->header = header;
  out_dsc->data_size = out_size;
  out_dsc->data = s_outbuf;

  return true;
}

void hw_jpeg_deinit(void) {
  if (s_dec) {
    jpeg_del_decoder_engine(s_dec);
    s_dec = NULL;
  }

  if (s_bitstream) {
    free(s_bitstream);
    s_bitstream = NULL;
    s_bitstream_size = 0;
  }
  if (s_outbuf) {
    free(s_outbuf);
    s_outbuf = NULL;
    s_outbuf_size = 0;
  }
}
