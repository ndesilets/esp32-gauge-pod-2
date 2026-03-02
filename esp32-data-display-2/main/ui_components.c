#include "ui_components.h"

#include <string.h>

#include "bsp_board_extra.h"
#include "driver/jpeg_decode.h"
#include "fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw_jpeg.h"

// Shared accent color derived from Subaru interior lighting
static lv_color_t SubaruReddishOrangeThing = {.blue = 6, .green = 1, .red = 254};
// static lv_color_t SubaruYellowGaugeThing = {.blue = 83, .green = 246, .red = 248};

// ====================================================================================================================
// general purpose stuff
// ====================================================================================================================

lv_style_t dd_screen_style;
lv_style_t dd_flex_row_style;
lv_style_t dd_flex_col_style;
lv_style_t button_style;
lv_style_t button_style_pressed;

void dd_init_styles() {
  // --- screen

  lv_style_init(&dd_screen_style);
  lv_style_set_bg_color(&dd_screen_style, lv_color_black());
  lv_style_set_text_color(&dd_screen_style, lv_color_white());
  // screen is not exactly centered in bezel
  lv_style_set_pad_top(&dd_screen_style, 8);
  lv_style_set_pad_right(&dd_screen_style, 4);
  lv_style_set_pad_left(&dd_screen_style, 12);
  lv_style_set_pad_bottom(&dd_screen_style, 8);
  // lv_style_set_pad_all(&dd_screen_style, 0);

  // --- row

  lv_style_init(&dd_flex_row_style);
  lv_style_set_bg_opa(&dd_flex_row_style, LV_OPA_TRANSP);
  lv_style_set_pad_column(&dd_flex_row_style, 4);
  lv_style_set_pad_all(&dd_flex_row_style, 0);
  lv_style_set_border_width(&dd_flex_row_style, 0);
  // lv_style_set_border_color(&dd_flex_row_style,
  //                           lv_palette_main(LV_PALETTE_RED));
  lv_style_set_radius(&dd_flex_row_style, 4);

  // --- column

  lv_style_init(&dd_flex_col_style);
  lv_style_set_bg_opa(&dd_flex_col_style, LV_OPA_TRANSP);
  lv_style_set_pad_row(&dd_flex_col_style, 0);
  lv_style_set_pad_all(&dd_flex_col_style, 0);
  // lv_style_set_pad_left(&dd_flex_col_style, 8);
  // lv_style_set_pad_right(&dd_flex_col_style, 8);
  lv_style_set_border_width(&dd_flex_col_style, 0);
  // lv_style_set_border_color(&dd_flex_col_style,
  //                           lv_palette_main(LV_PALETTE_CYAN));
  lv_style_set_radius(&dd_flex_col_style, 0);

  // --- button

  // normal button style
  lv_style_init(&button_style);
  lv_style_set_radius(&button_style, 4);
  lv_style_set_bg_opa(&button_style, LV_OPA_TRANSP);
  lv_style_set_bg_color(&button_style, lv_color_white());

  lv_style_set_border_opa(&button_style, LV_OPA_COVER);
  lv_style_set_border_width(&button_style, 2);
  lv_style_set_border_color(&button_style, lv_color_white());

  // pressed button style
  lv_style_set_outline_width(&button_style_pressed, 30);
  lv_style_set_outline_opa(&button_style_pressed, LV_OPA_TRANSP);

  lv_style_set_translate_y(&button_style_pressed, 1);
  lv_style_set_bg_opa(&button_style_pressed, LV_OPA_50);
  lv_style_set_bg_color(&button_style_pressed, lv_palette_darken(LV_PALETTE_BLUE, 2));
  lv_style_set_bg_grad_color(&button_style_pressed, lv_palette_darken(LV_PALETTE_BLUE, 4));
}

void dd_set_screen(lv_obj_t* obj) {
  lv_obj_add_style(obj, &dd_screen_style, 0);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj,
                        LV_FLEX_ALIGN_START,   // main axis (left→right)
                        LV_FLEX_ALIGN_CENTER,  // cross axis (top↕bottom)
                        LV_FLEX_ALIGN_START    // track alignment for multi-line rows
  );
}

void dd_set_flex_row(lv_obj_t* obj) {
  lv_obj_add_style(obj, &dd_flex_row_style, 0);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(obj,
                        LV_FLEX_ALIGN_START,   // main axis (left→right)
                        LV_FLEX_ALIGN_CENTER,  // cross axis (top↕bottom)
                        LV_FLEX_ALIGN_START    // track alignment for multi-line rows
  );
}

void dd_set_flex_column(lv_obj_t* obj) {
  lv_obj_add_style(obj, &dd_flex_col_style, 0);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj,
                        LV_FLEX_ALIGN_START,   // main axis (left→right)
                        LV_FLEX_ALIGN_CENTER,  // cross axis (top↕bottom)
                        LV_FLEX_ALIGN_START    // track alignment for multi-line rows
  );
}

// ====================================================================================================================
// ui components
// ====================================================================================================================

void dd_set_simple_metric_column(lv_obj_t* col) {
  lv_obj_set_size(col, LV_PCT(49), LV_SIZE_CONTENT);
  dd_set_flex_column(col);
  lv_obj_set_style_pad_row(col, 16, 0);
  lv_obj_set_style_border_side(
      col, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
  lv_obj_set_style_radius(col, 8, 0);
  lv_obj_set_style_border_width(col, 4, 0);
  lv_obj_set_style_border_color(col, SubaruReddishOrangeThing, 0);
  lv_obj_set_style_pad_left(col, 16, 0);
  lv_obj_set_style_pad_right(col, 16, 0);
  lv_obj_set_style_pad_bottom(col, 20, 0);
}

void dd_set_framed_controls_row(lv_obj_t* obj) {
  lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
  dd_set_flex_row(obj);
  lv_obj_set_flex_align(obj,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // main axis (left→right)
                        LV_FLEX_ALIGN_CENTER,         // cross axis (top↕bottom)
                        LV_FLEX_ALIGN_START           // track alignment for multi-line rows
  );
  lv_obj_set_style_border_side(obj, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT),
                               0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_border_width(obj, 4, 0);
  lv_obj_set_style_border_color(obj, SubaruReddishOrangeThing, 0);
  lv_obj_set_style_pad_top(obj, 12, 0);
  lv_obj_set_style_pad_left(obj, 12, 0);
  lv_obj_set_style_pad_right(obj, 12, 0);
}

void dd_set_action_button(lv_obj_t* btn, const char* label) {
  lv_obj_remove_style_all(btn);
  lv_obj_add_style(btn, &button_style, 0);
  lv_obj_add_style(btn, &button_style_pressed, LV_STATE_PRESSED);

  lv_obj_set_size(btn, 200, 90);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);

  lv_obj_t* record_label = lv_label_create(btn);
  lv_label_set_text(record_label, label);
  lv_obj_center(record_label);
}

// ====================================================================================================================
// overview screen components
// ====================================================================================================================

/*
 * framed panel
 */

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title, int cur_val, int min_bar_value,
                                   int max_bar_value) {
  framed_panel_t out = {0};

  out.container = lv_obj_create(parent);
  lv_obj_set_size(out.container, 222, 236);
  lv_obj_set_style_bg_opa(out.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.container, 0, 0);
  lv_obj_set_style_border_color(out.container, lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_set_style_pad_left(out.container, 0, 0);
  lv_obj_set_style_pad_right(out.container, 0, 0);
  lv_obj_set_style_pad_top(out.container, 8, 0);
  lv_obj_set_style_pad_bottom(out.container, 0, 0);

  out.frame = lv_obj_create(out.container);
  lv_obj_set_size(out.frame, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(out.frame, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.frame, 3, 0);
  lv_obj_set_style_border_color(out.frame, lv_color_white(), 0);
  lv_obj_set_style_radius(out.frame, 12, 0);
  lv_obj_set_style_pad_left(out.frame, 4, 0);
  lv_obj_set_style_pad_right(out.frame, 4, 0);
  lv_obj_set_style_pad_bottom(out.frame, 0, 0);
  lv_obj_set_style_pad_top(out.frame, 0, 0);

  out.title = lv_label_create(out.container);
  lv_label_set_text(out.title, title ? title : "WTHELLY?");
  lv_obj_set_style_text_font(out.title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_bg_opa(out.title, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(out.title, lv_color_black(), 0);
  lv_obj_set_style_pad_left(out.title, 12, 0);
  lv_obj_set_style_pad_right(out.title, 12, 0);
  lv_obj_set_style_text_color(out.title, lv_color_white(), 0);
  lv_coord_t lh = lv_font_get_line_height(&lv_font_montserrat_18);
  lv_obj_align_to(out.title, out.frame, LV_ALIGN_OUT_TOP_MID, 0, lh / 2);

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
  lv_obj_set_flex_align(out.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(out.body, 0, 0);
  lv_obj_set_style_pad_row(out.body, 0, 0);
  lv_obj_align(out.body, LV_ALIGN_CENTER, 0, 8);

  // main val

  out.main_value = lv_label_create(out.body);
  lv_obj_set_width(out.main_value, LV_PCT(100));
  lv_obj_set_style_text_color(out.main_value, lv_color_white(), 0);
  lv_obj_set_style_text_align(out.main_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(out.main_value, &lv_font_montserrat_72, 0);
  lv_obj_set_style_pad_top(out.main_value, 4, 0);
  lv_obj_set_style_border_width(out.main_value, 0, 0);
  lv_obj_set_style_border_color(out.main_value, lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_label_set_text_fmt(out.main_value, "%d", cur_val);

  // min / max

  lv_obj_t* minmax_container = lv_obj_create(out.body);
  lv_obj_set_style_bg_opa(minmax_container, LV_OPA_TRANSP, 0);
  lv_obj_set_size(minmax_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(minmax_container, 0, 0);
  lv_obj_set_style_border_color(minmax_container, lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_set_flex_flow(minmax_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(minmax_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(minmax_container, 0, 0);
  lv_obj_set_style_pad_all(minmax_container, 0, 0);
  lv_obj_set_style_pad_top(minmax_container, 6, 0);
  // this is what creates space between min/max and bar
  lv_obj_set_style_pad_bottom(minmax_container, 24, 0);

  out.minmax_value = lv_label_create(minmax_container);
  lv_obj_set_width(out.minmax_value, LV_PCT(100));
  lv_obj_set_style_text_align(out.minmax_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(out.minmax_value, lv_color_white(), 0);
  lv_obj_set_style_text_font(out.minmax_value, &lv_font_montserrat_22, 0);
  lv_obj_set_style_pad_all(out.minmax_value, 0, 0);
  lv_label_set_text_fmt(out.minmax_value, "%d / %d", cur_val, cur_val);

  // bar / scale

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
  lv_style_set_radius(&style_indic, 2);

  out.bar = lv_bar_create(out.body);
  lv_obj_remove_style_all(out.bar);
  lv_obj_add_style(out.bar, &style_bg, 0);
  lv_obj_add_style(out.bar, &style_indic, LV_PART_INDICATOR);

  lv_obj_set_size(out.bar, LV_PCT(100) - 8, 24);
  lv_bar_set_range(out.bar, min_bar_value, max_bar_value);
  lv_obj_center(out.bar);
  lv_bar_set_value(out.bar, cur_val, LV_ANIM_OFF);

  lv_obj_t* scale = lv_scale_create(out.body);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_text_font(scale, &lv_font_montserrat_18, LV_PART_INDICATOR);

  lv_obj_set_size(scale, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(scale, 14, 0);
  lv_obj_set_style_pad_right(scale, 14, 0);
  lv_scale_set_mode(scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
  lv_obj_center(scale);

  lv_scale_set_label_show(scale, true);
  lv_scale_set_total_tick_count(scale, 2);
  lv_scale_set_major_tick_every(scale, 1);

  lv_obj_set_style_length(scale, 3, LV_PART_ITEMS);
  lv_obj_set_style_length(scale, 6, LV_PART_INDICATOR);
  lv_scale_set_range(scale, min_bar_value, max_bar_value);

  return out;
}

void framed_panel_update(framed_panel_t* panel, int cur_val, int min_val, int max_val, monitor_status status) {
  lv_label_set_text_fmt(panel->main_value, "%d", cur_val);
  lv_label_set_text_fmt(panel->minmax_value, "%d / %d", min_val, max_val);
  lv_bar_set_value(panel->bar, cur_val, LV_ANIM_OFF);

  int bg_opa;
  lv_color_t bg_color;
  switch (status) {
    case STATUS_WARN:
      bg_opa = LV_OPA_50;
      bg_color = lv_palette_main(LV_PALETTE_YELLOW);
      break;
    case STATUS_CRITICAL:
      bg_opa = LV_OPA_50;
      bg_color = lv_palette_main(LV_PALETTE_RED);
      break;
    default:
      bg_opa = LV_OPA_TRANSP;
      bg_color = lv_color_black();
  }
  lv_obj_set_style_bg_opa(panel->container, bg_opa, 0);
  lv_obj_set_style_bg_color(panel->container, bg_color, 0);
}

/*
 * simple metric
 */

simple_metric_t simple_metric_create(lv_obj_t* parent, const char* title, float cur_val) {
  simple_metric_t out = {0};

  out.container = lv_obj_create(parent);
  lv_obj_set_size(out.container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(out.container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_side(out.container, LV_BORDER_SIDE_NONE, 0);
  lv_obj_set_style_border_side(out.container, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(out.container, 1, 0);
  lv_obj_set_style_border_color(out.container, SubaruReddishOrangeThing, 0);
  lv_obj_set_flex_flow(out.container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(out.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(out.container, 0, 0);
  lv_obj_set_style_pad_all(out.container, 0, 0);
  lv_obj_set_style_pad_bottom(out.container, 4, 0);

  out.title = lv_label_create(out.container);
  lv_label_set_text(out.title, title ? title : "wthelly");
  lv_obj_set_style_text_font(out.title, &lv_font_montserrat_18, 0);
  lv_obj_set_width(out.title, LV_PCT(100));
  lv_obj_set_style_text_align(out.title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(out.title, lv_color_white(), 0);

  lv_obj_t* vals_container = lv_obj_create(out.container);
  lv_obj_set_size(vals_container, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(vals_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vals_container, 0, 0);
  lv_obj_set_style_border_color(vals_container, lv_palette_darken(LV_PALETTE_PINK, 1), 0);
  lv_obj_set_flex_flow(vals_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vals_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(vals_container, 0, 0);
  lv_obj_set_style_pad_all(vals_container, 0, 0);
  lv_obj_set_style_pad_top(vals_container, 4, 0);

  out.min_val = lv_label_create(vals_container);
  lv_label_set_text_fmt(out.min_val, "%.1f", cur_val);
  lv_obj_set_style_text_color(out.min_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(out.min_val, &lv_font_montserrat_24, 0);
  lv_obj_set_flex_grow(out.min_val, 1);

  out.cur_val = lv_label_create(vals_container);
  lv_label_set_text_fmt(out.cur_val, "%.1f", cur_val);
  lv_obj_set_style_text_font(out.cur_val, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(out.cur_val, lv_color_white(), 0);

  out.max_val = lv_label_create(vals_container);
  lv_label_set_text_fmt(out.max_val, "%.1f", cur_val);
  lv_obj_set_style_text_align(out.max_val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(out.max_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(out.max_val, &lv_font_montserrat_24, 0);
  lv_obj_set_flex_grow(out.max_val, 1);

  return out;
}

void simple_metric_update(simple_metric_t* metric, float cur_val, float min_val, float max_val, monitor_status status) {
  lv_label_set_text_fmt(metric->min_val, "%.1f", min_val);
  lv_label_set_text_fmt(metric->cur_val, "%.1f", cur_val);
  lv_label_set_text_fmt(metric->max_val, "%.1f", max_val);

  int bg_opa;
  lv_color_t bg_color;
  switch (status) {
    case STATUS_WARN:
      bg_opa = LV_OPA_50;
      bg_color = lv_palette_main(LV_PALETTE_YELLOW);
      break;
    case STATUS_CRITICAL:
      bg_opa = LV_OPA_50;
      bg_color = lv_palette_main(LV_PALETTE_RED);
      break;
    default:
      bg_opa = LV_OPA_TRANSP;
      bg_color = lv_color_black();
  }
  lv_obj_set_style_bg_opa(metric->container, bg_opa, 0);
  lv_obj_set_style_bg_color(metric->container, bg_color, 0);
}

// ====================================================================================================================
// screens components
// ====================================================================================================================

void _set_base_screen_stuff(lv_obj_t* screen) {
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(screen, lv_color_white(), 0);

  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_text_color(screen, lv_color_white(), 0);
}

void dd_set_extremely_awesome_splash_screen(lv_obj_t* screen) {
  _set_base_screen_stuff(screen);

  // best splash animation you could possibly have

#if CONFIG_DD_ENABLE_INTRO_SOUND
  ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/FAHHH.wav"));
#endif

  // #if DD_ENABLE_INTRO_SPLASH
  size_t MAX_JPEG_SIZE = 70 * 1024;  // ~70kB
  if (!hw_jpeg_init(MAX_JPEG_SIZE, 720, 720)) {
    printf("HW JPEG init failed\n");
    return;
  }

  lv_obj_t* splash_image = lv_image_create(screen);
  lv_obj_set_size(splash_image, lv_pct(100), lv_pct(100));
  lv_image_set_scale(splash_image, LV_SCALE_NONE);  // no scaling distortion
  static lv_image_dsc_t splash_frame_dsc;

  for (int i = 0; i < 73; i++) {
    char filename[40];
    snprintf(filename, sizeof(filename), "/storage/jpegs/bklogo00108%03d.jpg", i);

    hw_jpeg_decode_file_to_lvimg(filename, &splash_frame_dsc);
    lv_image_set_src(splash_image, &splash_frame_dsc);
    lv_timer_handler();
  }

  // let last frame persist for a bit
  vTaskDelay(pdMS_TO_TICKS(1000));

  hw_jpeg_deinit();
  lv_obj_del(splash_image);
  // #endif
}

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

void dd_set_overview_screen(lv_obj_t* screen, lv_event_cb_t on_clear_button_clicked,
                            lv_event_cb_t on_options_button_clicked, lv_event_cb_t on_log_button_clicked) {
  lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
  dd_set_screen(screen);

  // --- top row

  lv_obj_t* first_row = lv_obj_create(screen);
  lv_obj_set_size(first_row, LV_PCT(100), 240);
  dd_set_flex_row(first_row);
  lv_obj_set_style_pad_column(first_row, 16, 0);
  lv_obj_set_style_pad_all(first_row, 0, 0);

  water_temp_panel = framed_panel_create(first_row, "W.TEMP", 0, 160, 220);
  oil_temp_panel = framed_panel_create(first_row, "O.TEMP", 0, 160, 250);
  oil_psi_panel = framed_panel_create(first_row, "O.PSI", 0, 0, 100);

  // --- second row

  lv_obj_t* second_row = lv_obj_create(screen);
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
  dam = simple_metric_create(left_col, "DAM", 0);
  af_learned = simple_metric_create(left_col, "AF.LEARNED", 0);
  afr = simple_metric_create(left_col, "AF.RATIO", 0);
  iat = simple_metric_create(left_col, "INT.TEMP", 0);

  lv_obj_t* right_col = lv_obj_create(second_row);
  dd_set_simple_metric_column(right_col);
  fb_knock = simple_metric_create(right_col, "FB.KNOCK", 0);
  af_correct = simple_metric_create(right_col, "AF.CORRECT", 0);
  inj_duty = simple_metric_create(right_col, "INJ.DUTY", 0);
  eth_conc = simple_metric_create(right_col, "ETH.CONC", 0);

  // --- third row

  lv_obj_t* third_row = lv_obj_create(screen);
  dd_set_framed_controls_row(third_row);

  lv_obj_t* clear_button = lv_btn_create(third_row);
  dd_set_action_button(clear_button, "CLEAR");
  lv_obj_add_event_cb(clear_button, on_clear_button_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t* options_button = lv_btn_create(third_row);
  dd_set_action_button(options_button, "OPTIONS");
  lv_obj_add_event_cb(options_button, on_options_button_clicked, LV_EVENT_CLICKED, NULL);

  lv_obj_t* log_button = lv_btn_create(third_row);
  dd_set_action_button(log_button, "LOG");
  lv_obj_add_event_cb(log_button, on_log_button_clicked, LV_EVENT_CLICKED, NULL);
}

void dd_update_overview_screen(monitored_state_t m_state) {
  static monitored_state_t prev = {0};
  static bool first_run = true;

// clang-format off
#define UPDATE_IF_CHANGED(widget_fn, widget, field) \

  // Macro for:
  // framed_panel_update(&water_temp_panel, m_state.water_temp.current_value, m_state.water_temp.min_value,
                      // m_state.water_temp.max_value, m_state.water_temp.status);

  if (first_run || memcmp(&prev.field, &m_state.field, sizeof(numeric_monitor_t)) != 0) { \
    widget_fn(&widget, m_state.field.current_value, m_state.field.min_value, \
              m_state.field.max_value, m_state.field.status); \
  }

  UPDATE_IF_CHANGED(framed_panel_update, water_temp_panel, water_temp);
  UPDATE_IF_CHANGED(framed_panel_update, oil_temp_panel, oil_temp);
  UPDATE_IF_CHANGED(framed_panel_update, oil_psi_panel, oil_pressure);

  UPDATE_IF_CHANGED(simple_metric_update, dam, dam);
  UPDATE_IF_CHANGED(simple_metric_update, af_learned, af_learned);
  UPDATE_IF_CHANGED(simple_metric_update, afr, af_ratio);
  UPDATE_IF_CHANGED(simple_metric_update, iat, int_temp);

  UPDATE_IF_CHANGED(simple_metric_update, fb_knock, fb_knock);
  UPDATE_IF_CHANGED(simple_metric_update, af_correct, af_correct);
  UPDATE_IF_CHANGED(simple_metric_update, inj_duty, inj_duty);
  UPDATE_IF_CHANGED(simple_metric_update, eth_conc, eth_conc);

#undef UPDATE_IF_CHANGED
// clang-format on

  prev = m_state;
  first_run = false;
}

void dd_set_metric_detail_screen(lv_obj_t* screen) {}
void dd_update_metric_detail_screen(monitored_state_t m_state) {}

// options

static int _clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int _brightness_raw_to_percent(int raw_brightness) {
  int clamped = _clamp_int(raw_brightness, BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
  int range = BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX - BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN;
  if (range <= 0) {
    return 0;
  }
  return ((clamped - BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN) * 100) / range;
}

void dd_set_options_screen(lv_obj_t* screen, dd_options_screen_t* out, int initial_brightness, int initial_volume) {
  _set_base_screen_stuff(screen);
  lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  int brightness = _clamp_int(initial_brightness, BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
  int volume = _clamp_int(initial_volume, 0, 100);

  lv_obj_t* card = lv_obj_create(screen);
  lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_pad_left(card, 40, 0);
  lv_obj_set_style_pad_right(card, 40, 0);
  lv_obj_set_style_pad_top(card, 22, 0);
  lv_obj_set_style_pad_bottom(card, 18, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 80, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "OPTIONS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* brightness_block = lv_obj_create(card);
  lv_obj_set_size(brightness_block, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(brightness_block, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(brightness_block, 0, 0);
  lv_obj_set_style_pad_all(brightness_block, 0, 0);
  lv_obj_set_style_pad_bottom(brightness_block, 16, 0);
  lv_obj_set_style_pad_row(brightness_block, 12, 0);
  lv_obj_clear_flag(brightness_block, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(brightness_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(brightness_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* brightness_header_row = lv_obj_create(brightness_block);
  lv_obj_set_size(brightness_header_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(brightness_header_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(brightness_header_row, 0, 0);
  lv_obj_set_style_pad_all(brightness_header_row, 0, 0);
  lv_obj_clear_flag(brightness_header_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(brightness_header_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brightness_header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  lv_obj_t* brightness_header = lv_label_create(brightness_header_row);
  lv_label_set_text(brightness_header, "Brightness");
  lv_obj_set_style_text_font(brightness_header, &lv_font_montserrat_30, 0);

  lv_obj_t* brightness_value = lv_label_create(brightness_header_row);
  lv_label_set_text_fmt(brightness_value, "%d%%", _brightness_raw_to_percent(brightness));
  lv_obj_set_style_text_font(brightness_value, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(brightness_value, SubaruReddishOrangeThing, 0);

  lv_obj_t* brightness_slider = lv_slider_create(brightness_block);
  lv_obj_set_width(brightness_slider, LV_PCT(100));
  lv_obj_set_height(brightness_slider, 24);
  lv_slider_set_range(brightness_slider, BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
  lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);

  lv_obj_t* volume_block = lv_obj_create(card);
  lv_obj_set_size(volume_block, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(volume_block, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(volume_block, 0, 0);
  lv_obj_set_style_pad_all(volume_block, 0, 0);
  lv_obj_set_style_pad_bottom(volume_block, 16, 0);
  lv_obj_set_style_pad_row(volume_block, 12, 0);
  lv_obj_clear_flag(volume_block, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(volume_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(volume_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* volume_header_row = lv_obj_create(volume_block);
  lv_obj_set_size(volume_header_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(volume_header_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(volume_header_row, 0, 0);
  lv_obj_set_style_pad_all(volume_header_row, 0, 0);
  lv_obj_clear_flag(volume_header_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(volume_header_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(volume_header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  lv_obj_t* volume_header = lv_label_create(volume_header_row);
  lv_label_set_text(volume_header, "Volume");
  lv_obj_set_style_text_font(volume_header, &lv_font_montserrat_30, 0);

  lv_obj_t* volume_value = lv_label_create(volume_header_row);
  lv_label_set_text_fmt(volume_value, "%d%%", volume);
  lv_obj_set_style_text_font(volume_value, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(volume_value, SubaruReddishOrangeThing, 0);

  lv_obj_t* volume_slider = lv_slider_create(volume_block);
  lv_obj_set_width(volume_slider, LV_PCT(100));
  lv_obj_set_height(volume_slider, 24);
  lv_slider_set_range(volume_slider, 0, 100);
  lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);

  lv_obj_t* spacer = lv_obj_create(card);
  lv_obj_set_size(spacer, LV_PCT(100), 1);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(spacer, 0, 0);
  lv_obj_set_style_pad_all(spacer, 0, 0);
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* done_button = lv_btn_create(card);
  lv_obj_add_style(done_button, &button_style, 0);
  lv_obj_add_style(done_button, &button_style_pressed, LV_STATE_PRESSED);
  lv_obj_set_size(done_button, 280, 84);
  lv_obj_set_style_border_color(done_button, SubaruReddishOrangeThing, 0);

  lv_obj_t* done_label = lv_label_create(done_button);
  lv_label_set_text(done_label, "DONE");
  lv_obj_set_style_text_font(done_label, &lv_font_montserrat_24, 0);
  lv_obj_center(done_label);

  if (out) {
    out->brightness_slider = brightness_slider;
    out->brightness_value_label = brightness_value;
    out->volume_slider = volume_slider;
    out->volume_value_label = volume_value;
    out->done_button = done_button;
  }
}
