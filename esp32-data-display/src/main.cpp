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
  lv_obj_set_size(out.container, 148, 168);
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
  lv_obj_set_size(out.frame, 142, 154);
  lv_obj_set_style_bg_opa(out.frame, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.frame, 3, 0);
  lv_obj_set_style_border_color(out.frame, lv_color_white(), 0);
  lv_obj_set_style_radius(out.frame, 12, 0);
  lv_obj_set_style_pad_left(out.frame, 8, 0);
  lv_obj_set_style_pad_right(out.frame, 8, 0);
  // lv_obj_set_style_pad_top(out.frame, 8, 0);
  // lv_obj_set_style_pad_bottom(out.frame, 8, 0);
  // important to get text to render over border

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
  // make sure it draws on top

  // Inner body (so contents don’t collide with the border or title)
  out.body = lv_obj_create(out.frame);
  lv_obj_set_size(out.body, LV_PCT(100), 100);
  lv_obj_set_style_bg_opa(out.body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(out.body, 0, 0);
  lv_obj_set_style_pad_top(out.body,
                           lv_font_get_line_height(lv_font_get_default()),
                           0);  // leave room for the title
  lv_obj_set_style_pad_left(out.body, 8, 0);
  lv_obj_set_style_pad_right(out.body, 8, 0);
  lv_obj_set_style_pad_bottom(out.body, 8, 0);
  lv_obj_align(out.body, LV_ALIGN_CENTER, 0, 8);

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