#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <bklogo.h>
#include <lvgl.h>

#include "displays_config.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gt911.h"
#include "lvgl_littlefs.h"
#include "touch.h"

extern "C" {
#include "driver/jpeg_decode.h"
#include "hw_jpeg.h"
}

#define MAX_TOUCH_POINTS 5

Arduino_ESP32DSIPanel* dsipanel = new Arduino_ESP32DSIPanel(
    display_cfg.hsync_pulse_width, display_cfg.hsync_back_porch,
    display_cfg.hsync_front_porch, display_cfg.vsync_pulse_width,
    display_cfg.vsync_back_porch, display_cfg.vsync_front_porch,
    display_cfg.prefer_speed, display_cfg.lane_bit_rate);

Arduino_DSI_Display* gfx = new Arduino_DSI_Display(
    display_cfg.width, display_cfg.height, dsipanel, 0, true,
    display_cfg.lcd_rst, display_cfg.init_cmds, display_cfg.init_cmds_size);

static lv_display_t* disp = nullptr;
static uint16_t* fb1 = nullptr;
static uint16_t* fb2 = nullptr;

static lv_indev_t* indev_touchpad;
static uint16_t touch_x[MAX_TOUCH_POINTS] = {0};
static uint16_t touch_y[MAX_TOUCH_POINTS] = {0};
static uint16_t touch_strength[MAX_TOUCH_POINTS] = {0};
static uint8_t touch_cnt = 0;
static bool touch_pressed = false;

static esp_lcd_touch_handle_t tp_handle = NULL;
#define MAX_TOUCH_POINTS 5

// pulled this outta ye olde eyedropper
lv_color_t SubaruReddishOrangeThing = {.blue = 6, .green = 1, .red = 254};

uint32_t millis_cb(void) { return millis(); }

void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
  lv_display_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
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

typedef struct {
  lv_obj_t* container;
  lv_obj_t* frame;  // outer border
  lv_obj_t* title;  // title text
  lv_obj_t* body;   // inner area for metrics/bar

  lv_obj_t* main_value;
  lv_obj_t* minmax_value;

  // config
  int min_gauge_value;
  int max_gauge_value;

} framed_panel_t;

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title,
                                   const char* main_val, const char* minmax_val,
                                   int min_gauge_value, int max_gauge_value) {
  framed_panel_t out = {0};

  out.container = lv_obj_create(parent);
  lv_obj_set_size(out.container, 148, 172);
  lv_obj_set_style_bg_opa(out.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.container, 0, 0);
  lv_obj_set_style_border_color(out.container, lv_palette_main(LV_PALETTE_CYAN),
                                0);
  lv_obj_set_style_pad_left(out.container, 0, 0);
  lv_obj_set_style_pad_right(out.container, 0, 0);
  lv_obj_set_style_pad_top(out.container, 8, 0);
  lv_obj_set_style_pad_bottom(out.container, 0, 0);

  // border
  out.frame = lv_obj_create(out.container);
  lv_obj_set_size(out.frame, 142, LV_PCT(100));
  lv_obj_set_style_bg_opa(out.frame, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.frame, 3, 0);
  lv_obj_set_style_border_color(out.frame, lv_color_white(), 0);
  lv_obj_set_style_radius(out.frame, 12, 0);
  lv_obj_set_style_pad_left(out.frame, 4, 0);
  lv_obj_set_style_pad_right(out.frame, 4, 0);
  lv_obj_set_style_pad_bottom(out.frame, 0, 0);
  lv_obj_set_style_pad_top(out.frame, 0, 0);

  // title
  out.title = lv_label_create(out.container);
  lv_label_set_text(out.title, title ? title : "WTHELLY?");
  lv_obj_set_style_bg_opa(out.title, LV_OPA_COVER, 0);
  // notch the frame border
  lv_obj_set_style_bg_color(out.title, lv_color_black(), 0);
  lv_obj_set_style_pad_left(out.title, 12, 0);
  lv_obj_set_style_pad_right(out.title, 12, 0);
  lv_obj_set_style_text_color(out.title, lv_color_white(), 0);
  // overlap title with border
  lv_coord_t lh = lv_font_get_line_height(lv_font_get_default());
  lv_obj_align_to(out.title, out.frame, LV_ALIGN_OUT_TOP_MID, 0, lh / 2);

  // --- inner body

  out.body = lv_obj_create(out.frame);
  lv_obj_set_size(out.body, LV_PCT(100), LV_PCT(100) - 12);
  lv_obj_set_style_bg_opa(out.body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.body, 0, 0);
  lv_obj_set_style_border_color(out.body, lv_palette_main(LV_PALETTE_PINK), 0);
  lv_obj_set_style_pad_top(out.body, 4, 0);
  lv_obj_set_style_pad_left(out.body, 8, 0);
  lv_obj_set_style_pad_right(out.body, 8, 0);
  lv_obj_set_style_pad_bottom(out.body, 4, 0);
  lv_obj_set_flex_flow(out.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(out.body,
                        LV_FLEX_ALIGN_START,   // top
                        LV_FLEX_ALIGN_CENTER,  // horizontal
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(out.body, 0, 0);  // spacing between items
  lv_obj_set_style_pad_row(out.body, 0, 0);     // spacing between items
  lv_obj_align(out.body, LV_ALIGN_CENTER, 0, 8);

  // main value

  out.main_value = lv_label_create(out.body);
  lv_obj_set_width(out.main_value, LV_PCT(100));
  lv_obj_set_style_text_color(out.main_value, lv_color_white(), 0);
  lv_obj_set_style_text_align(out.main_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(out.main_value, &lv_font_montserrat_44, 0);
  lv_obj_set_style_border_width(out.main_value, 0, 0);
  lv_obj_set_style_border_color(out.main_value,
                                lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_label_set_text(out.main_value, main_val);

  // min/max

  lv_obj_t* minmax_container = lv_obj_create(out.body);
  lv_obj_set_style_bg_opa(minmax_container, LV_OPA_TRANSP, 0);
  lv_obj_set_size(minmax_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(minmax_container, 0, 0);
  lv_obj_set_style_border_color(minmax_container,
                                lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_set_flex_flow(minmax_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(minmax_container,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // main axis
                        LV_FLEX_ALIGN_CENTER,         // cross axis
                        LV_FLEX_ALIGN_START           // track align
  );
  lv_obj_set_style_pad_column(minmax_container, 0, 0);  // spacing between items
  lv_obj_set_style_pad_all(minmax_container, 0, 0);
  lv_obj_set_style_pad_top(minmax_container, 4, 0);
  lv_obj_set_style_pad_bottom(minmax_container, 16, 0);

  out.minmax_value = lv_label_create(minmax_container);
  lv_obj_set_width(out.minmax_value, LV_PCT(100));
  lv_obj_set_style_text_align(out.minmax_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(out.minmax_value, lv_color_white(), 0);
  lv_obj_set_style_text_font(out.minmax_value, &lv_font_montserrat_16, 0);
  lv_obj_set_style_pad_all(out.minmax_value, 0, 0);
  lv_label_set_text(out.minmax_value, minmax_val);

  // bar gauge

  out.min_gauge_value = min_gauge_value;
  out.max_gauge_value = max_gauge_value;

  static lv_style_t style_bg;
  static lv_style_t style_indic;

  lv_style_init(&style_bg);
  lv_style_set_border_color(&style_bg, SubaruReddishOrangeThing);
  lv_style_set_border_width(&style_bg, 2);
  lv_style_set_pad_left(&style_bg, 6);
  lv_style_set_pad_right(&style_bg, 6);
  lv_style_set_pad_top(&style_bg, 6);
  lv_style_set_pad_bottom(&style_bg, 6);
  lv_style_set_radius(&style_bg, 6);
  lv_style_set_anim_duration(&style_bg, 1000);

  lv_style_init(&style_indic);
  lv_style_set_bg_opa(&style_indic, LV_OPA_COVER);
  lv_style_set_bg_color(&style_indic, lv_color_white());
  lv_style_set_radius(&style_indic, 3);

  lv_obj_t* bar = lv_bar_create(out.body);
  lv_obj_remove_style_all(bar);
  lv_obj_add_style(bar, &style_bg, 0);
  lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);

  lv_obj_set_size(bar, LV_PCT(100), 20);
  lv_bar_set_range(bar, out.min_gauge_value, out.max_gauge_value);
  lv_obj_center(bar);
  lv_bar_set_value(bar, strtol(main_val, NULL, 10), LV_ANIM_OFF);

  // gauge scale

  lv_obj_t* scale = lv_scale_create(out.body);

  // TODO set own style instead of what im assuming is global
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_INDICATOR);

  lv_obj_set_size(scale, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(scale, 8, 0);
  lv_obj_set_style_pad_right(scale, 8, 0);
  lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
  lv_obj_center(scale);

  lv_scale_set_label_show(scale, true);

  lv_scale_set_total_tick_count(scale, 2);
  lv_scale_set_major_tick_every(scale, 1);

  lv_obj_set_style_length(scale, 3, LV_PART_ITEMS);
  lv_obj_set_style_length(scale, 6, LV_PART_INDICATOR);
  lv_scale_set_range(scale, out.min_gauge_value, out.max_gauge_value);

  return out;
}

typedef struct {
  lv_obj_t* container;
  lv_obj_t* title;  // title text
  lv_obj_t* body;   // inner area for metrics
  lv_obj_t* min_val;
  lv_obj_t* cur_val;
  lv_obj_t* max_val;
} simple_metric_t;

simple_metric_t simple_metric_create(lv_obj_t* parent, const char* title,
                                     const char* min_val, const char* cur_val,
                                     const char* max_val) {
  simple_metric_t out = {0};

  out.container = lv_obj_create(parent);
  lv_obj_set_size(out.container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(out.container, LV_OPA_TRANSP, 0);
  // Turn off all borders first
  lv_obj_set_style_border_side(out.container, LV_BORDER_SIDE_NONE, 0);
  // Enable only the bottom border
  lv_obj_set_style_border_side(out.container, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(out.container, 1, 0);
  lv_obj_set_style_border_color(out.container, SubaruReddishOrangeThing, 0);
  lv_obj_set_flex_flow(out.container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(out.container,
                        LV_FLEX_ALIGN_START,   // main axis
                        LV_FLEX_ALIGN_CENTER,  // cross axis
                        LV_FLEX_ALIGN_START    // track align
  );
  lv_obj_set_style_pad_row(out.container, 0, 0);  // spacing between items
  lv_obj_set_style_pad_all(out.container, 0, 0);

  // title
  out.title = lv_label_create(out.container);
  lv_label_set_text(out.title, title ? title : "wthelly");
  lv_obj_set_width(out.title, LV_PCT(100));
  lv_obj_set_style_text_align(out.title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(out.title, lv_color_white(), 0);

  // values

  lv_obj_t* vals_container = lv_obj_create(out.container);
  lv_obj_set_size(vals_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(vals_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vals_container, 0, 0);
  lv_obj_set_style_border_color(vals_container,
                                lv_palette_darken(LV_PALETTE_PINK, 1), 0);
  lv_obj_set_flex_flow(vals_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vals_container,
                        LV_FLEX_ALIGN_CENTER,  // main axis
                        LV_FLEX_ALIGN_CENTER,  // cross axis
                        LV_FLEX_ALIGN_START    // track align
  );
  lv_obj_set_style_pad_row(vals_container, 0, 0);  // spacing between items
  lv_obj_set_style_pad_all(vals_container, 0, 0);

  out.min_val = lv_label_create(vals_container);
  lv_label_set_text(out.min_val, min_val);
  lv_obj_set_style_text_color(out.min_val, lv_color_white(), 0);
  lv_obj_set_flex_grow(out.min_val, 1);

  out.cur_val = lv_label_create(vals_container);
  lv_label_set_text(out.cur_val, cur_val);
  lv_obj_set_style_text_font(out.cur_val, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(out.cur_val, lv_color_white(), 0);

  out.max_val = lv_label_create(vals_container);
  lv_label_set_text(out.max_val, max_val);
  // TODO need to do own styles
  lv_obj_set_style_text_font(out.max_val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(out.max_val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(out.max_val, lv_color_white(), 0);
  lv_obj_set_flex_grow(out.max_val, 1);

  return out;
}

void setup() {
  Serial.begin(115200);
  delay(500);

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

  size_t draw_buf_size = display_cfg.width * display_cfg.height / 10;
  fb1 = (uint16_t*)heap_caps_malloc(draw_buf_size * sizeof(lv_color_t),
                                    MALLOC_CAP_DMA);
  if (!fb1) {
    Serial.println("LVGL draw buffer 1 allocation failed!");
  }

  fb2 = (uint16_t*)heap_caps_malloc(draw_buf_size * sizeof(lv_color_t),
                                    MALLOC_CAP_DMA);
  if (!fb2) {
    Serial.println("LVGL draw buffer 2 allocation failed!");
    heap_caps_free(fb1);
  }

  disp = lv_display_create(display_cfg.height, display_cfg.width);
  lv_display_set_default(disp);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, fb1, fb2, draw_buf_size,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, my_disp_flush);

  // init lvgl touch

  indev_touchpad = lv_indev_create();
  lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev_touchpad, my_touchpad_read);

  // init ui

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);

  // splash movie

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

    if (hw_jpeg_decode_file_to_lvimg(filename, &splash_frame_dsc)) {
      lv_image_set_src(splash_image, &splash_frame_dsc);
      lv_timer_handler();  // force LVGL to render this frame

      // Optional pacing:
      // measure how long decode+render took and delay to ~30fps
      // For first pass you can even skip delay and just see how fast it goes
    } else {
      Serial.printf("Failed to decode %s\n", filename);
    }
  }

  delay(1500);

  hw_jpeg_deinit();
  lv_obj_del(splash_image);

  // setup main UI

  lv_obj_t* row = lv_obj_create(lv_screen_active());
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row,
      LV_FLEX_ALIGN_START,   // main axis (left→right)
      LV_FLEX_ALIGN_CENTER,  // cross axis (top↕bottom)
      LV_FLEX_ALIGN_START    // track alignment for multi-line rows
  );
  lv_obj_set_style_pad_column(row, 4, 0);  // spacing between items
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_border_color(row, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_radius(row, 4, 0);

  framed_panel_t water_temp_panel =
      framed_panel_create(row, "W.TEMP", "196", "87 / 206", 160, 220);
  framed_panel_t oil_temp_panel =
      framed_panel_create(row, "O.TEMP", "207", "74 / 236", 160, 250);
  framed_panel_t oil_psi_panel =
      framed_panel_create(row, "O.PSI", "73", "21 / 93", 0, 100);

  // simple metrics off to the right

  // lv_obj_t* col = lv_obj_create(row);
  // lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  // lv_obj_set_size(col, 180, LV_PCT(100));
  // lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  // lv_obj_set_flex_align(
  //     col,
  //     LV_FLEX_ALIGN_SPACE_EVENLY,  // main axis (left→right)
  //     LV_FLEX_ALIGN_CENTER,        // cross axis (top↕bottom)
  //     LV_FLEX_ALIGN_START          // track alignment for multi-line rows
  // );
  // lv_obj_set_style_pad_row(col, 0, 0);  // spacing between items
  // lv_obj_set_style_pad_all(col, 0, 0);
  // lv_obj_set_style_pad_left(col, 8, 0);
  // lv_obj_set_style_pad_right(col, 8, 0);
  // lv_obj_set_style_border_width(col, 0, 0);
  // lv_obj_set_style_border_color(col, lv_palette_main(LV_PALETTE_CYAN), 0);
  // lv_obj_set_style_radius(col, 0, 0);

  // simple_metric_t afr =
  //     simple_metric_create(col, "AFR", "11.1", "14.7", "20.0");
  // simple_metric_t fb_knock =
  //     simple_metric_create(col, "FB.KNOCK", "-1.4", "0", "0");
  // simple_metric_t af_correction =
  //     simple_metric_create(col, "AF.LEARNED", "-7.5", "-2.0", "3.4");
  // simple_metric_t af_learned =
  //     simple_metric_create(col, "DAM", "1.0", "1.0", "1.0");

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();

  delay(5);
}