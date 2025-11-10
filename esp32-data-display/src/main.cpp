#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

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
    172 /* width */, 640 /* height */, 0 /* col offset 1 */,
    0 /* row offset 1 */, 0 /* col offset 2 */, 0 /* row offset 2 */);

uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
// lv_display_t* disp;
uint16_t* disp_draw_buf;
static uint32_t buf_pixels, buf_bytes;

static lv_display_t* disp = nullptr;
static uint16_t* fb = nullptr;  // full-frame RGB565 buffer

uint32_t millis_cb(void) { return millis(); }

void my_print(lv_log_level_t level, const char* buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

/* LVGL calls it when a rendered image needs to copied to the display*/
void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
#ifndef DIRECT_RENDER_MODE
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);

  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
#endif  // #ifndef DIRECT_RENDER_MODE

  /*Call it to tell LVGL you are ready*/
  lv_disp_flush_ready(disp);
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

  screenWidth = gfx->width();
  screenHeight = gfx->height();

  disp = lv_display_create(screenWidth, screenHeight);
  lv_display_set_default(disp);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);

  size_t buf_bytes = screenWidth * screenHeight * 2;

  const size_t fb_bytes = screenWidth * screenHeight * 2;
  fb = (uint16_t*)heap_caps_malloc(fb_bytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!fb) fb = (uint16_t*)heap_caps_malloc(fb_bytes, MALLOC_CAP_8BIT);

  lv_display_set_buffers(disp, fb, nullptr, fb_bytes,
                         LV_DISPLAY_RENDER_MODE_FULL);

  // ---------- simple UI to prove it works ----------
  lv_obj_t* label = lv_label_create(lv_screen_active());
  lv_label_set_text_fmt(label, "Hello LVGL %d.%d.%d", LVGL_VERSION_MAJOR,
                        LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_center(label);

  // Ensure an immediate frame
  lv_refr_now(NULL);

  Serial.println("Setup done");
}

void loop() {
  lv_task_handler();

  delay(16);
  // for (int d = 0; d <= 1023; d += 16) {
  //   ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, d);
  //   ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  //   // gfx->fillScreen(GREEN);
  //   delay(33);
  // }
  // for (int d = 1023; d >= 0; d -= 16) {
  //   ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, d);
  //   ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  //   // gfx->fillScreen(GREEN);
  //   delay(33);
  // }
}