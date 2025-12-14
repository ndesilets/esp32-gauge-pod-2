#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_demos.h"
#include "lvgl.h"
#include "monitoring.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_components.h"

static const char* TAG = "app";
static lv_color_t SubaruReddishOrangeThing = {.blue = 6, .green = 1, .red = 254};

typedef enum { OVERVIEW, METRIC_DETAIL } ui_state_t;
ui_state_t ui_state = OVERVIEW;
lv_obj_t* overview_screen;
lv_obj_t* metric_detail_screen;

monitored_state_t m_state = {
    .water_temp = {.status = STATUS_OK},
    .oil_temp = {.status = STATUS_OK},
    .oil_pressure = {.status = STATUS_OK},
    .dam = {.current_value = 1.0, .status = STATUS_OK},
    .af_learned = {.current_value = 0.0, .status = STATUS_OK},
    .af_ratio = {.current_value = 14.7, .status = STATUS_OK},
    .int_temp = {.current_value = 67.0, .status = STATUS_OK},
    .fb_knock = {.current_value = 0.0, .status = STATUS_OK},
    .af_correct = {.current_value = 0.0, .status = STATUS_OK},
    .inj_duty = {.current_value = 0.0, .status = STATUS_OK},
    .eth_conc = {.current_value = 67.0, .status = STATUS_OK},
};

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

// ====== ui callback stuff =======

static void on_reset_button_clicked(lv_event_t* e) {
  ESP_LOGI(TAG, "Reset button clicked");
  // ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/FAHHH.wav"));
}

static void on_options_button_clicked(lv_event_t* e) {
  ESP_LOGI(TAG, "Options button clicked");
  // ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/FAHHH.wav"));
}

static void on_record_button_clicked(lv_event_t* e) {
  ESP_LOGI(TAG, "Record button clicked");
  // ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/FAHHH.wav"));
}

// ====== sys level stuff =======

static void telemetry_task(void* arg) {
  static monitored_state_t prev_state = {0};
  static bool prev_state_valid = false;

  int i = 0;
  for (;;) {
    uint64_t now_ms = esp_timer_get_time() / 1000;

    // --- get the data

    // unsigned int engine_rpm = wrap_range(i, 700, 7000);
    unsigned int engine_rpm = 2500;
    m_state.water_temp.current_value = wrap_range(i / 4, 190, 240);
    m_state.oil_temp.current_value = wrap_range(i / 4, 200, 300);
    m_state.oil_pressure.current_value = wrap_range(i / 4, 0, 100);

    // m_state.dam.current_value = map_sine_to_range(sinf(i / 50.0f), 0, 1.049);
    // m_state.af_learned.current_value = map_sine_to_range(sinf(i / 50.0f), -10, 10);
    // m_state.af_ratio.current_value = map_sine_to_range(sinf(i / 50.0f), 11.1, 20.0);
    // m_state.int_temp.current_value = map_sine_to_range(sinf(i / 50.0f), 30, 120);

    // m_state.fb_knock.current_value = map_sine_to_range(sinf(i / 50.0f), -6, 0.49);
    // m_state.af_correct.current_value = map_sine_to_range(sinf(i / 50.0f), -10, 10);
    // m_state.inj_duty.current_value = map_sine_to_range(sinf(i / 50.0f), 0, 105);
    // m_state.eth_conc.current_value = map_sine_to_range(sinf(i / 50.0f), 10, 85);

    // --- do monitoring

    evaluate_statuses(&m_state, engine_rpm);

    bool alert_transition = false;
    if (prev_state_valid) {
      alert_transition = has_alert_transition(&prev_state, &m_state);
    }
    prev_state = m_state;
    prev_state_valid = true;

    if (alert_transition) {
      ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/ahh2.wav"));
    }

    // --- update rpm counter

    // TODO

    // --- update display

    if (bsp_display_lock(pdMS_TO_TICKS(100))) {
      switch (ui_state) {
        case OVERVIEW:
          dd_update_overview_screen(m_state);
          break;
        case METRIC_DETAIL:
          dd_update_metric_detail_screen(m_state);
          break;
        default:
          break;
      }

      bsp_display_unlock();
    }

    int64_t elapsed_ms = (esp_timer_get_time() / 1000) - now_ms;
    ESP_LOGI(TAG, "UI updating took %lld ms", elapsed_ms);

    i++;
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

// ====== main =======

void app_main(void) {
  // --- init audio

  ESP_ERROR_CHECK(bsp_extra_codec_init());
  ESP_ERROR_CHECK(bsp_extra_player_init());

  // --- init display

  // anything that involves changing background opacity seems to benefit from full size buffer + spiram
  // otherwise not using spiram and smaller buffer is good
  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           //  .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
                           .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
                           .double_buffer = true,
                           .flags = {
                               .buff_dma = true,
                               //  .buff_spiram = false,
                               .buff_spiram = true,
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

  bsp_display_lock(0);  // need to lock to build UI in thread safe manner

  dd_init_styles();

  lv_obj_t* splash_screen = lv_obj_create(NULL);
  lv_screen_load(splash_screen);
  set_extremely_awesome_splash_screen(splash_screen);

  // setup screens

  overview_screen = lv_obj_create(NULL);
  dd_set_overview_screen(overview_screen, on_reset_button_clicked, on_options_button_clicked, on_record_button_clicked);

  metric_detail_screen = lv_obj_create(NULL);
  dd_set_metric_detail_screen(metric_detail_screen);

  ESP_LOGI(TAG, "Screen setup complete");

  switch (ui_state) {
    case OVERVIEW:
      lv_screen_load(overview_screen);
      break;
    case METRIC_DETAIL:
      lv_screen_load(metric_detail_screen);
      break;
    default:
      break;
  }

  ESP_LOGI(TAG, "Default screen set");

  lv_obj_del(splash_screen);
  bsp_display_unlock();

  // --- everything else

  xTaskCreate(telemetry_task, "telemetry", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
