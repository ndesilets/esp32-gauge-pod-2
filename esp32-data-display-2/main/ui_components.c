#include "ui_components.h"

#include "fonts.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Shared accent color derived from Subaru interior lighting
static lv_color_t SubaruReddishOrangeThing = {.blue = 6, .green = 1, .red = 254};
static lv_color_t SubaruYellowGaugeThing = {.blue = 83, .green = 246, .red = 248};

/*
 * framed panel
 */

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title, int cur_val, int min_bar_value,
                                   int max_bar_value) {
  framed_panel_t out = {0};
  out.min_value = cur_val;
  out.max_value = cur_val;

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
  lv_label_set_text_fmt(out.minmax_value, "%d / %d", out.min_value, out.max_value);

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

void framed_panel_update(framed_panel_t* panel, int gauge_value) {
  panel->min_value = MIN(panel->min_value, gauge_value);
  panel->max_value = MAX(panel->max_value, gauge_value);

  lv_label_set_text_fmt(panel->main_value, "%d", gauge_value);
  lv_label_set_text_fmt(panel->minmax_value, "%d / %d", panel->min_value, panel->max_value);
  lv_bar_set_value(panel->bar, gauge_value, LV_ANIM_OFF);
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

void simple_metric_update(simple_metric_t* metric, float gauge_value) {
  metric->min_value = MIN(metric->min_value, gauge_value);
  metric->max_value = MAX(metric->max_value, gauge_value);
  lv_label_set_text_fmt(metric->min_val, "%.1f", metric->min_value);
  lv_label_set_text_fmt(metric->cur_val, "%.1f", gauge_value);
  lv_label_set_text_fmt(metric->max_val, "%.1f", metric->max_value);
}

lv_style_t dd_screen_style;
lv_style_t dd_flex_row_style;
lv_style_t dd_flex_col_style;

void dd_init_styles() {
  // screen

  lv_style_init(&dd_screen_style);
  lv_style_set_bg_color(&dd_screen_style, lv_color_black());
  lv_style_set_text_color(&dd_screen_style, lv_color_white());
  // screen is not exactly centered in bezel
  lv_style_set_pad_top(&dd_screen_style, 8);
  lv_style_set_pad_right(&dd_screen_style, 4);
  lv_style_set_pad_left(&dd_screen_style, 12);
  lv_style_set_pad_bottom(&dd_screen_style, 8);
  // lv_style_set_pad_all(&dd_screen_style, 0);

  // row

  lv_style_init(&dd_flex_row_style);
  lv_style_set_bg_opa(&dd_flex_row_style, LV_OPA_TRANSP);
  lv_style_set_pad_column(&dd_flex_row_style, 4);
  lv_style_set_pad_all(&dd_flex_row_style, 0);
  lv_style_set_border_width(&dd_flex_row_style, 0);
  // lv_style_set_border_color(&dd_flex_row_style,
  //                           lv_palette_main(LV_PALETTE_RED));
  lv_style_set_radius(&dd_flex_row_style, 4);

  // column

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
