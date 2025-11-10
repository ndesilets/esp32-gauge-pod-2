#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <bklogo.h>
#include <lvgl.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#define PANEL_W 172
#define PANEL_H 640

#define PIN_BK GPIO_NUM_8
#define PIN_RST GPIO_NUM_21
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_1
#define LEDC_DUTY_RES LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY 50000  // or 5khz

Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    GPIO_NUM_9 /* CS */, GPIO_NUM_10 /* SCK */, GPIO_NUM_11 /* D0 */,
    GPIO_NUM_12 /* D1 */, GPIO_NUM_13 /* D2 */, GPIO_NUM_14 /* D3 */);

Arduino_GFX* gfx = new Arduino_AXS15231B(
    bus, GPIO_NUM_21 /* RST */, 0 /* rotation */, false /* IPS */,
    PANEL_W /* width */, PANEL_H /* height */, 0 /* col offset 1 */,
    0 /* row offset 1 */, 0 /* col offset 2 */, 0 /* row offset 2 */);

static lv_display_t* disp = nullptr;
static uint16_t* fb = nullptr;  // full framebuffer (landscape 640x172)
static uint16_t* rotatedbuf = nullptr;

uint32_t millis_cb(void) { return millis(); }

// rotate 90 degrees
void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const int w = lv_area_get_width(area);   // LVGL area width  (landscape space)
  const int h = lv_area_get_height(area);  // LVGL area height (landscape space)
  const int x0 = area->x1;
  const int y0 = area->y1;

  const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);

  for (int yy = 0; yy < h; ++yy) {
    const uint16_t* srow = src + yy * w;
    for (int xx = 0; xx < w; ++xx) {
      rotatedbuf[xx * h + (h - 1 - yy)] = srow[xx];
    }
  }

  const int panel_x = PANEL_W - (y0 + h);
  const int panel_y = x0;

  gfx->draw16bitRGBBitmap(panel_x, panel_y, rotatedbuf, h, w);
  lv_display_flush_ready(disp);
}

typedef struct {
  lv_obj_t* container;
  lv_obj_t* frame;  // outer border
  lv_obj_t* title;  // title text
  lv_obj_t* body;   // inner area for metrics/bar
} framed_panel_t;

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title) {
  framed_panel_t out = {0};

  out.container = lv_obj_create(parent);
  lv_obj_set_size(out.container, 148, LV_PCT(100));
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

  lv_obj_t* main_value = lv_label_create(out.body);
  lv_obj_set_width(main_value, LV_PCT(100));
  lv_obj_set_style_text_color(main_value, lv_color_white(), 0);
  lv_obj_set_style_text_align(main_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(main_value, &lv_font_montserrat_44, 0);
  lv_obj_set_style_border_width(main_value, 0, 0);
  lv_obj_set_style_border_color(main_value, lv_palette_main(LV_PALETTE_CYAN),
                                0);
  lv_label_set_text(main_value, "196");

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

  lv_obj_t* min_label = lv_label_create(minmax_container);
  lv_obj_set_width(min_label, LV_PCT(100));
  lv_obj_set_style_text_align(min_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(min_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(min_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_pad_all(min_label, 0, 0);
  lv_label_set_text(min_label, "87 / 203");

  // lv_obj_t* max_label = lv_label_create(minmax_container);
  // lv_obj_set_style_text_color(max_label, lv_color_white(), 0);
  // lv_obj_set_style_text_font(max_label, &lv_font_montserrat_16, 0);
  // lv_obj_set_style_pad_all(max_label, 0, 0);
  // lv_label_set_text(max_label, "203");

  // bar gauge

  static lv_style_t style_bg;
  static lv_style_t style_indic;

  lv_style_init(&style_bg);
  lv_style_set_border_color(&style_bg,
                            lv_palette_darken(LV_PALETTE_DEEP_ORANGE, 1));
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
  lv_bar_set_range(bar, 0, 240);
  lv_obj_center(bar);
  lv_bar_set_value(bar, 196, LV_ANIM_OFF);

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
  lv_scale_set_range(scale, 160, 240);

  return out;
}

// then use it
void setup() {
  Serial.begin(115200);
  delay(500);

  // // init backlight

  ledc_timer_config_t tcfg = {.speed_mode = LEDC_MODE,
                              .duty_resolution = LEDC_DUTY_RES,
                              .timer_num = LEDC_TIMER_3,
                              .freq_hz = LEDC_FREQUENCY,
                              .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&tcfg);

  ledc_channel_config_t ccfg = {.gpio_num = PIN_BK,
                                .speed_mode = LEDC_MODE,
                                .channel = LEDC_CHANNEL_1,
                                .intr_type = LEDC_INTR_DISABLE,
                                .timer_sel = LEDC_TIMER_3,
                                .duty = (0xff - 255),
                                .hpoint = 0};
  ledc_channel_config(&ccfg);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, (0xff - 255));
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);

  // init display

  gfx->begin(40 * 1000 * 1000);
  gfx->fillScreen(BLACK);

  // init lvgl

  lv_init();
  lv_tick_set_cb(millis_cb);

  disp = lv_display_create(PANEL_H, PANEL_W);
  lv_display_set_default(disp);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);

  size_t fb_bytes = PANEL_H * PANEL_W * 2;
  fb = (uint16_t*)heap_caps_malloc(fb_bytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!fb) fb = (uint16_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_8BIT);
  lv_display_set_buffers(disp, fb, nullptr, fb_bytes,
                         LV_DISPLAY_RENDER_MODE_FULL);

  rotatedbuf = (uint16_t*)heap_caps_malloc(fb_bytes,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rotatedbuf)
    rotatedbuf = (uint16_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_8BIT);

  // init ui

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);

  // splash image

  const lv_image_dsc_t bklogo_dsc = {
      .header =
          {
              .cf = LV_COLOR_FORMAT_RGB565,
              .w = 640,
              .h = 172,
          },
      .data_size = sizeof(image_data_bklogo),
      .data = (const uint8_t*)image_data_bklogo,
  };
  lv_obj_t* img = lv_image_create(lv_screen_active());
  lv_image_set_src(img, &bklogo_dsc);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

  // hide after 2s
  lv_refr_now(NULL);
  delay(2000);
  lv_obj_del(img);

  // lv_obj_t* label = lv_label_create(lv_screen_active());
  // lv_label_set_text_fmt(label, "I'M GEEKED 67 I'M GEEKED 67 I'M GEEKED 67");
  // lv_obj_center(label);

  // lv_refr_now(NULL);
  // delay(1000);
  // lv_obj_del(label);

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

  framed_panel_t water_temp_panel = framed_panel_create(row, "W.TEMP");
  framed_panel_t oil_temp_panel = framed_panel_create(row, "O.TEMP");
  framed_panel_t oil_psi_panel = framed_panel_create(row, "O.PSI");
  // lv_obj_align(test.frame, LV_ALIGN_LEFT_MID, 0, 0);

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();

  delay(16);
}