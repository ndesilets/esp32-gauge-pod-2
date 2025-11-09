#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#define PIN_BK GPIO_NUM_8
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE  // on ESP32-S3, use LOW speed
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT  // 10-bit duty (0..1023)
#define LEDC_FREQUENCY 25000             // 5 kHz is good for backlights

Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    GPIO_NUM_9 /* CS */, GPIO_NUM_10 /* SCK */, GPIO_NUM_11 /* D0 */,
    GPIO_NUM_12 /* D1 */, GPIO_NUM_13 /* D2 */, GPIO_NUM_14 /* D3 */);

Arduino_GFX* gfx = new Arduino_AXS15231B(
    bus, GPIO_NUM_21 /* RST */, 2 /* rotation */, false /* IPS */,
    172 /* width */, 640 /* height */, 0 /* col offset 1 */,
    0 /* row offset 1 */, 0 /* col offset 2 */, 0 /* row offset 2 */);

// then use it
void setup() {
  Serial.begin(115200);
  delay(500);

  // 1) Configure timer
  ledc_timer_config_t tcfg = {.speed_mode = LEDC_MODE,
                              .duty_resolution = LEDC_DUTY_RES,
                              .timer_num = LEDC_TIMER,
                              .freq_hz = LEDC_FREQUENCY,
                              .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&tcfg);

  // 2) Configure channel
  ledc_channel_config_t ccfg = {.gpio_num = PIN_BK,
                                .speed_mode = LEDC_MODE,
                                .channel = LEDC_CHANNEL,
                                .intr_type = LEDC_INTR_DISABLE,
                                .timer_sel = LEDC_TIMER,
                                .duty = 1023,  // full brightness (10-bit)
                                .hpoint = 0};
  ledc_channel_config(&ccfg);

  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 1023);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

  gfx->begin(40 * 1000 * 1000);
  gfx->fillScreen(BLACK);
}

void loop() {
  for (int d = 0; d <= 1023; d += 16) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, d);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    gfx->fillScreen(GREEN);
    delay(33);
  }
  for (int d = 1023; d >= 0; d -= 16) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, d);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    gfx->fillScreen(GREEN);
    delay(33);
  }
}