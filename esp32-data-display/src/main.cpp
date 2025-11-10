#include <Arduino.h>
#include <Arduino_GFX_Library.h>
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
static uint16_t* fb = nullptr;      // FULL framebuffer (landscape 640x172)
static uint16_t* rotbuf = nullptr;  // scratch for rotated blocks (same size)

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
      rotbuf[xx * h + (h - 1 - yy)] = srow[xx];
    }
  }

  const int panel_x = PANEL_W - (y0 + h);
  const int panel_y = x0;

  gfx->draw16bitRGBBitmap(panel_x, panel_y, rotbuf, h, w);
  lv_display_flush_ready(disp);
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

  // // Hard-force backlight ON to rule out PWM issues
  // pinMode(PIN_BK, OUTPUT);
  // digitalWrite(PIN_BK, HIGH);  // flip to LOW if your board is active-low

  // Hard reset the panel
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(20);
  digitalWrite(PIN_RST, HIGH);
  delay(120);

  // init display

  gfx->begin(40 * 1000 * 1000);
  gfx->fillScreen(BLACK);

  // init lvgl

  lv_init();
  lv_tick_set_cb(millis_cb);

  // LVGL logical landscape canvas
  const int HRES = PANEL_H, VRES = PANEL_W;

  disp = lv_display_create(HRES, VRES);
  lv_display_set_default(disp);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);

  // Full framebuffer (LVGL-side)
  size_t fb_bytes = HRES * VRES * 2;
  fb = (uint16_t*)heap_caps_malloc(fb_bytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!fb) fb = (uint16_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_8BIT);
  lv_display_set_buffers(disp, fb, nullptr, fb_bytes,
                         LV_DISPLAY_RENDER_MODE_FULL);

  // Scratch buffer for rotated blocks (same size as max flush area)
  rotbuf = (uint16_t*)heap_caps_malloc(fb_bytes,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rotbuf) rotbuf = (uint16_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_8BIT);

  // ---------- simple UI to prove it works ----------
  lv_obj_t* label = lv_label_create(lv_screen_active());

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_white(), 0);

  lv_label_set_text_fmt(label, "I'M GEEKED 67 I'M GEEKED 67 I'M GEEKED 67");
  lv_obj_center(label);

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();

  delay(16);
}