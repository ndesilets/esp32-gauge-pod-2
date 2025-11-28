#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <lvgl.h>

#include "displays_config.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gt911.h"
#include "lvgl_littlefs.h"
#include "touch.h"
#include "ui_components.h"

extern "C" {
#include "driver/jpeg_decode.h"
#include "hw_jpeg.h"
}

#define MAX_TOUCH_POINTS 5

// display

Arduino_ESP32DSIPanel* dsi_panel = new Arduino_ESP32DSIPanel(
    display_cfg.hsync_pulse_width, display_cfg.hsync_back_porch,
    display_cfg.hsync_front_porch, display_cfg.vsync_pulse_width,
    display_cfg.vsync_back_porch, display_cfg.vsync_front_porch,
    display_cfg.prefer_speed, display_cfg.lane_bit_rate);

Arduino_DSI_Display* gfx = new Arduino_DSI_Display(
    display_cfg.width, display_cfg.height, dsi_panel, 0, true,
    display_cfg.lcd_rst, display_cfg.init_cmds, display_cfg.init_cmds_size);

static lv_display_t* disp = nullptr;
static uint16_t* fb1 = nullptr;
static uint16_t* fb2 = nullptr;

// touch

static lv_indev_t* indev_touchpad;
static uint16_t touch_x[MAX_TOUCH_POINTS] = {0};
static uint16_t touch_y[MAX_TOUCH_POINTS] = {0};
static uint16_t touch_strength[MAX_TOUCH_POINTS] = {0};
static uint8_t touch_cnt = 0;
static bool touch_pressed = false;
static esp_lcd_touch_handle_t tp_handle = NULL;

uint32_t millis_cb(void) { return millis(); }

void lv_display_flush(lv_display_t* disp, const lv_area_t* area,
                      uint8_t* px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
  lv_display_flush_ready(disp);
}

void lv_touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
  esp_lcd_touch_read_data(tp_handle);
  touch_pressed =
      esp_lcd_touch_get_coordinates(tp_handle, touch_x, touch_y, touch_strength,
                                    &touch_cnt, MAX_TOUCH_POINTS);

  if (touch_pressed && touch_cnt > 0) {
    data->point.x = touch_x[0];
    data->point.y = touch_y[0];
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // init touch interface

  DEV_I2C_Port port = DEV_I2C_Init();
  tp_handle = touch_gt911_init(port);

  // init display

  gfx->begin();

  // init fs

  if (!LittleFS.begin(true)) {
    Serial.println("uh oh stinky while mounting LittleFS");
    return;
  }

  // init lvgl

  lv_init();
  lv_tick_set_cb(millis_cb);

  size_t draw_buf_size =
      (display_cfg.width * display_cfg.height / 10) * sizeof(lv_color_t);
  fb1 = (uint16_t*)heap_caps_malloc(draw_buf_size, MALLOC_CAP_DMA);
  fb2 = (uint16_t*)heap_caps_malloc(draw_buf_size, MALLOC_CAP_DMA);

  disp = lv_display_create(display_cfg.height, display_cfg.width);
  lv_display_set_default(disp);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, fb1, fb2, draw_buf_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, lv_display_flush);

  // init lvgl touch

  indev_touchpad = lv_indev_create();
  lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev_touchpad, lv_touch_read);

  // init ui

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);
  dd_init_styles();

#ifdef SHOW_BURGER_KING

  // best splash animation you could possibly have

  constexpr size_t MAX_JPEG_SIZE = 70 * 1024;  // ~70kB
  if (!hw_jpeg_init(MAX_JPEG_SIZE, 720, 720)) {
    Serial.println("HW JPEG init failed");
    return;
  }

  lv_obj_t* splash_image = lv_image_create(lv_screen_active());
  lv_obj_set_size(splash_image, lv_pct(100), lv_pct(100));
  lv_image_set_scale(splash_image, LV_SCALE_NONE);  // no scaling distortion
  static lv_image_dsc_t splash_frame_dsc;

  for (int i = 0; i < 73; i++) {
    char filename[40];
    snprintf(filename, sizeof(filename), "/bk/bklogo00108%03d.jpg", i);

    hw_jpeg_decode_file_to_lvimg(filename, &splash_frame_dsc);
    lv_image_set_src(splash_image, &splash_frame_dsc);
    lv_timer_handler();
  }

  delay(1500);

  hw_jpeg_deinit();
  lv_obj_del(splash_image);

#endif

  // set parent level container

  lv_obj_t* home_screen = lv_obj_create(lv_screen_active());
  lv_obj_set_size(home_screen, LV_PCT(100), LV_PCT(100));
  dd_set_screen(home_screen);

  // top row

  lv_obj_t* first_row = lv_obj_create(home_screen);
  lv_obj_set_size(first_row, LV_PCT(100), 240);
  dd_set_flex_row(first_row);
  lv_obj_set_style_pad_column(first_row, 16, 0);

  framed_panel_t water_temp_panel =
      framed_panel_create(first_row, "W.TEMP", "196", "87 / 206", 160, 220);
  framed_panel_t oil_temp_panel =
      framed_panel_create(first_row, "O.TEMP", "217", "74 / 236", 160, 250);
  framed_panel_t oil_psi_panel =
      framed_panel_create(first_row, "O.PSI", "73", "21 / 93", 0, 100);

  // second row

  lv_obj_t* second_row = lv_obj_create(home_screen);
  lv_obj_set_size(second_row, LV_PCT(100), 260);
  dd_set_flex_row(second_row);
  lv_obj_set_flex_align(
      second_row,
      LV_FLEX_ALIGN_SPACE_AROUND,  // main axis (left→right)
      LV_FLEX_ALIGN_CENTER,        // cross axis (top↕bottom)
      LV_FLEX_ALIGN_START          // track alignment for multi-line rows
  );
  lv_obj_set_style_pad_top(second_row, 16, 0);

  lv_obj_t* left_col = lv_obj_create(second_row);
  lv_obj_set_size(left_col, LV_PCT(40), LV_PCT(100));
  dd_set_flex_column(left_col);
  lv_obj_set_style_pad_row(left_col, 16, 0);

  simple_metric_t afr =
      simple_metric_create(left_col, "AFR", "11.1", "14.7", "20.0");
  simple_metric_t af_learned =
      simple_metric_create(left_col, "AF.LEARNED", "-7.5", "-2.0", "3.4");
  simple_metric_t fb_knock =
      simple_metric_create(left_col, "FB.KNOCK", "-1.4", "0", "0");

  lv_obj_t* right_col = lv_obj_create(second_row);
  lv_obj_set_size(right_col, LV_PCT(40), LV_PCT(100));
  dd_set_flex_column(right_col);
  lv_obj_set_style_pad_row(right_col, 16, 0);

  simple_metric_t eth_conc =
      simple_metric_create(right_col, "ETH.CONC", "61.0", "61.0", "61.0");
  simple_metric_t inj_duty =
      simple_metric_create(right_col, "INJ.DUTY", "0", "1.63", "79.62");
  simple_metric_t dam =
      simple_metric_create(right_col, "DAM", "1.0", "1.0", "1.0");

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();

  delay(5);
}