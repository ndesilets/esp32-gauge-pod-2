#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "driver/jpeg_decode.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw_jpeg.h"
#include "lv_demos.h"
#include "lvgl.h"
#include "math.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_components.h"

static const char* TAG = "app";
static lv_color_t SubaruReddishOrangeThing = {.blue = 6, .green = 1, .red = 254};

static lv_obj_t* screen = NULL;
framed_panel_t water_temp_panel;
framed_panel_t oil_temp_panel;
framed_panel_t oil_psi_panel;

simple_metric_t afr;
simple_metric_t af_correct;
simple_metric_t af_learned;
simple_metric_t fb_knock;
simple_metric_t eth_conc;
simple_metric_t inj_duty;
simple_metric_t dam;
simple_metric_t iat;

// ====== util functions =======

int wrap_range(int counter, int lo, int hi) {
  if (hi <= lo) {
    return lo;  // or handle as error
  }
  int span = hi - lo + 1;
  int offset = counter % span;
  if (offset < 0) {
    offset += span;  // guard against negative counters
  }
  return lo + offset;
}

float map_sine_to_range(float sine_val, float lo, float hi) {
  if (hi <= lo) {
    return lo;  // or handle as error
  }
  // sine_val expected in [-1, 1]
  float normalized = (sine_val + 1.0f) * 0.5f;  // now 0..1
  return lo + normalized * (hi - lo);
}

// ====== ui  stuff =======

void extremely_awesome_splash_screen() {
  // best splash animation you could possibly have

  size_t MAX_JPEG_SIZE = 70 * 1024;  // ~70kB
  if (!hw_jpeg_init(MAX_JPEG_SIZE, 720, 720)) {
    printf("HW JPEG init failed\n");
    return;
  }

  lv_obj_t* splash_image = lv_image_create(lv_screen_active());
  lv_obj_set_size(splash_image, lv_pct(100), lv_pct(100));
  lv_image_set_scale(splash_image, LV_SCALE_NONE);  // no scaling distortion
  static lv_image_dsc_t splash_frame_dsc;

  for (int i = 0; i < 73; i++) {
    char filename[40];
    snprintf(filename, sizeof(filename), "/storage/bklogo00108%03d.jpg", i);

    hw_jpeg_decode_file_to_lvimg(filename, &splash_frame_dsc);
    lv_image_set_src(splash_image, &splash_frame_dsc);
    lv_timer_handler();
  }

  // let last frame persist for a bit
  vTaskDelay(pdMS_TO_TICKS(1500));

  hw_jpeg_deinit();
  lv_obj_del(splash_image);
}

// ====== sys level stuff =======

static void telemetry_task(void* arg) {
  int i = 0;
  for (;;) {
    if (bsp_display_lock(pdMS_TO_TICKS(100))) {
      framed_panel_update(&water_temp_panel, wrap_range(i, -10, 220));
      framed_panel_update(&oil_temp_panel, wrap_range(i, -10, 250));
      framed_panel_update(&oil_psi_panel, wrap_range(i, 0, 100));

      simple_metric_update(&afr, map_sine_to_range(sinf(i / 50.0f), 11.1, 20.0));
      simple_metric_update(&af_learned, map_sine_to_range(sinf(i / 50.0f), -10, 10));
      simple_metric_update(&fb_knock, map_sine_to_range(sinf(i / 50.0f), -4.2, 0));

      simple_metric_update(&eth_conc, map_sine_to_range(sinf(i / 50.0f), 10, 85));
      simple_metric_update(&inj_duty, map_sine_to_range(sinf(i / 50.0f), 0, 100));
      simple_metric_update(&dam, map_sine_to_range(sinf(i / 50.0f), 0, 1));

      bsp_display_unlock();
    }
    i++;
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

static void init_littlefs(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .format_if_mount_failed = true,
      .dont_mount = false,
  };
  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    ESP_LOGE("FS", "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
    return;
  }
}

// ====== main =======

void app_main(void) {
  // --- init display

  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
                           //  .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
                           .double_buffer = true,
                           .flags = {
                               .buff_dma = true,
                               .buff_spiram = false,
                               .sw_rotate = false,
                           }};
  bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();
  bsp_display_brightness_set(50);

  // --- init littlefs

  esp_vfs_littlefs_conf_t littlefs_conf = {
      .base_path = "/storage", .partition_label = NULL, .format_if_mount_failed = true};
  esp_err_t ret = esp_vfs_littlefs_register(&littlefs_conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find LittleFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    }
    return;
  }

  // --- init UI

  // need to lock to build UI in thread safe manner
  bsp_display_lock(0);

  lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);
  dd_init_styles();

  lv_display_t* disp = lv_display_get_default();
  lv_display_set_offset(disp, 0, 0);

  extremely_awesome_splash_screen();

  // set parent level container

  lv_obj_t* home_screen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(home_screen, LV_PCT(100), LV_PCT(100));
  dd_set_screen(home_screen);

  // --- top row

  lv_obj_t* first_row = lv_obj_create(home_screen);
  lv_obj_set_size(first_row, LV_PCT(100), 240);
  dd_set_flex_row(first_row);
  lv_obj_set_style_pad_column(first_row, 16, 0);
  lv_obj_set_style_pad_all(first_row, 0, 0);

  water_temp_panel = framed_panel_create(first_row, "W.TEMP", 87, 160, 220);
  oil_temp_panel = framed_panel_create(first_row, "O.TEMP", 74, 160, 250);
  oil_psi_panel = framed_panel_create(first_row, "O.PSI", 21, 0, 100);

  // --- second row

  lv_obj_t* second_row = lv_obj_create(home_screen);
  lv_obj_set_size(second_row, LV_PCT(100), LV_SIZE_CONTENT);
  dd_set_flex_row(second_row);
  lv_obj_set_flex_align(second_row,
                        // LV_FLEX_ALIGN_SPACE_AROUND,  // main axis (left→right)
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // main axis (left→right)
                        LV_FLEX_ALIGN_CENTER,         // cross axis (top↕bottom)
                        LV_FLEX_ALIGN_START           // track alignment for multi-line rows
  );
  lv_obj_set_style_pad_all(second_row, 0, 0);
  lv_obj_set_style_pad_top(second_row, 4, 0);

  lv_obj_t* left_col = lv_obj_create(second_row);
  dd_set_simple_metric_column(left_col);
  dam = simple_metric_create(left_col, "DAM", 1.0);
  af_learned = simple_metric_create(left_col, "AF.LEARNED", -7.5);
  afr = simple_metric_create(left_col, "AF.RATIO", 11.1);
  iat = simple_metric_create(left_col, "INT.TEMP", 67.9);

  lv_obj_t* right_col = lv_obj_create(second_row);
  dd_set_simple_metric_column(right_col);
  fb_knock = simple_metric_create(right_col, "FB.KNOCK", -1.4);
  af_correct = simple_metric_create(right_col, "AF.CORRECT", -7.5);
  inj_duty = simple_metric_create(right_col, "INJ.DUTY", 1.11);
  eth_conc = simple_metric_create(right_col, "ETH.CONC", 61.0);

  // --- third row

  lv_obj_t* third_row = lv_obj_create(home_screen);
  dd_set_framed_controls_row(third_row);

  lv_obj_t* reset_button = lv_btn_create(third_row);
  dd_set_action_button(reset_button, "RESET");

  lv_obj_t* record_button = lv_btn_create(third_row);
  dd_set_action_button(record_button, "RECORD");

  // --- everything else

  bsp_display_unlock();

  xTaskCreate(telemetry_task, "telemetry", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}