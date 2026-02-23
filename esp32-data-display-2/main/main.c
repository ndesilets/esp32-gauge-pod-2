#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "car_data.h"
#include "cbor.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "logger.h"
#include "math.h"
#include "monitoring.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "telemetry_types.h"
#include "ui_components.h"

static const char* TAG = "app";

typedef enum { OVERVIEW, METRIC_DETAIL, OPTIONS } ui_state_t;
ui_state_t ui_state = OVERVIEW;
lv_obj_t* overview_screen;
lv_obj_t* metric_detail_screen;
lv_obj_t* options_screen;
dd_options_screen_t options_ui;

#define DD_SETTINGS_NAMESPACE "dd_settings"
#define DD_SETTINGS_KEY_BRIGHTNESS "brightness"
#define DD_SETTINGS_KEY_VOLUME "volume"
#define DD_DEFAULT_BRIGHTNESS 50

typedef struct {
  int brightness;
  int volume;
} app_settings_t;

static app_settings_t app_settings = {
    .brightness = DD_DEFAULT_BRIGHTNESS,
    .volume = 0,
};

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
SemaphoreHandle_t m_state_mutex;
static lv_obj_t* log_button_obj = NULL;
static bool log_button_is_active = false;

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static int brightness_to_percent(int raw_brightness) {
  int clamped = clamp_int(raw_brightness, BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
  int range = BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX - BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN;
  if (range <= 0) {
    return 0;
  }
  return ((clamped - BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN) * 100) / range;
}

static esp_err_t init_settings_storage(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
    err = nvs_flash_init();
  }
  return err;
}

static esp_err_t load_app_settings(app_settings_t* settings) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(DD_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to open settings namespace");

  int32_t brightness = settings->brightness;
  int32_t volume = settings->volume;

  err = nvs_get_i32(handle, DD_SETTINGS_KEY_BRIGHTNESS, &brightness);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to read brightness setting");
  }

  err = nvs_get_i32(handle, DD_SETTINGS_KEY_VOLUME, &volume);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to read volume setting");
  }

  nvs_close(handle);

  settings->brightness = clamp_int(brightness, BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
  settings->volume = clamp_int(volume, 0, 100);
  return ESP_OK;
}

static esp_err_t save_app_settings(const app_settings_t* settings) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(DD_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to open settings NVS");

  err = nvs_set_i32(handle, DD_SETTINGS_KEY_BRIGHTNESS, settings->brightness);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to write brightness");
  }

  err = nvs_set_i32(handle, DD_SETTINGS_KEY_VOLUME, settings->volume);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to write volume");
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to commit settings");
  }

  nvs_close(handle);
  return ESP_OK;
}

static void update_options_value_labels(void) {
  if (!options_ui.brightness_value_label || !options_ui.volume_value_label) {
    return;
  }

  lv_label_set_text_fmt(options_ui.brightness_value_label, "%d%%", brightness_to_percent(app_settings.brightness));
  lv_label_set_text_fmt(options_ui.volume_value_label, "%d%%", app_settings.volume);
}

static void load_overview_screen(void) {
  ui_state = OVERVIEW;
  lv_screen_load(overview_screen);
}

static void set_log_button_active_style(lv_obj_t* btn, bool active) {
  if (!btn) {
    return;
  }

  if (active) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  } else {
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
  }

  log_button_is_active = active;
}

// ====== ui callback stuff =======

static void on_reset_button_clicked(lv_event_t* e) {
  if (xSemaphoreTake(m_state_mutex, pdMS_TO_TICKS(100))) {
    reset_monitored_state(&m_state);
    xSemaphoreGive(m_state_mutex);
  }
}

static void on_options_button_clicked(lv_event_t* e) {
  ESP_LOGI(TAG, "Options button clicked");
  ui_state = OPTIONS;
  lv_screen_load(options_screen);
}

static void on_log_button_clicked(lv_event_t* e) {
  log_button_obj = lv_event_get_target(e);

  if (dd_logger_is_active()) {
    ESP_LOGI(TAG, "Stopping logger");
    dd_logger_stop();
    set_log_button_active_style(log_button_obj, false);
    return;
  }

  esp_err_t err = dd_logger_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start logger: %s", esp_err_to_name(err));
    set_log_button_active_style(log_button_obj, false);
    return;
  }

  ESP_LOGI(TAG, "Logger started");
  set_log_button_active_style(log_button_obj, true);
}

static void on_options_slider_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* target = lv_event_get_target(e);

  if (target == options_ui.brightness_slider) {
    app_settings.brightness = clamp_int(lv_slider_get_value(options_ui.brightness_slider),
                                        BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
    bsp_display_brightness_set(app_settings.brightness);
  } else if (target == options_ui.volume_slider) {
    app_settings.volume = clamp_int(lv_slider_get_value(options_ui.volume_slider), 0, 100);
    esp_err_t volume_err = bsp_extra_codec_volume_set(app_settings.volume, NULL);
    if (volume_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set volume: %s", esp_err_to_name(volume_err));
    }
  }

  update_options_value_labels();

  if (code == LV_EVENT_RELEASED) {
    esp_err_t save_err = save_app_settings(&app_settings);
    if (save_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to persist settings: %s", esp_err_to_name(save_err));
    }
  }
}

static void on_options_done_clicked(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  ESP_LOGI(TAG, "Options done clicked");
  load_overview_screen();
}

// ====== tasks =======

static void main_loop_task(void* arg) {
  static monitored_state_t prev_state = {0};
  static bool prev_state_valid = false;

  for (;;) {
    uint64_t now_ms = esp_timer_get_time() / 1000;

    // --- get the data

    display_packet_t packet;
    bool received = get_data(&packet);

    // --- do monitoring

    bool alert_transition = false;

    if (xSemaphoreTake(m_state_mutex, pdMS_TO_TICKS(100))) {
      update_numeric_monitor(&m_state.water_temp, packet.water_temp);
      update_numeric_monitor(&m_state.oil_temp, packet.oil_temp);
      update_numeric_monitor(&m_state.oil_pressure, packet.oil_pressure);

      update_numeric_monitor(&m_state.dam, packet.dam);
      update_numeric_monitor(&m_state.af_learned, packet.af_learned);
      update_numeric_monitor(&m_state.af_ratio, packet.af_ratio);
      update_numeric_monitor(&m_state.int_temp, packet.int_temp);

      update_numeric_monitor(&m_state.fb_knock, packet.fb_knock);
      update_numeric_monitor(&m_state.af_correct, packet.af_correct);
      update_numeric_monitor(&m_state.inj_duty, packet.inj_duty);
      update_numeric_monitor(&m_state.eth_conc, packet.eth_conc);

      evaluate_statuses(&m_state, packet.engine_rpm);

      if (prev_state_valid) {
        alert_transition = has_alert_transition(&prev_state, &m_state);
      }

      xSemaphoreGive(m_state_mutex);
    } else {
      ESP_LOGW(TAG, "Could not take m_state mutex in main loop (missed packet)");
    }

    prev_state = m_state;
    prev_state_valid = true;

    if (alert_transition) {
#ifdef CONFIG_DD_ENABLE_ALERT_AUDIO
      ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/tacobell.wav"));
#endif
      ESP_LOGW(TAG, "uh oh stinky");
    }

    // --- update rpm counter

    // TODO

    // --- update display

    if (bsp_display_lock(pdMS_TO_TICKS(100))) {
      if (log_button_obj && log_button_is_active && !dd_logger_is_active()) {
        set_log_button_active_style(log_button_obj, false);
      }

      switch (ui_state) {
        case OVERVIEW:
          dd_update_overview_screen(m_state);
          break;
        case METRIC_DETAIL:
          dd_update_metric_detail_screen(m_state);
          break;
        case OPTIONS:
          // TODO
          break;
        default:
          break;
      }

      bsp_display_unlock();
    }

    int64_t elapsed_ms = (esp_timer_get_time() / 1000) - now_ms;
    // ESP_LOGI(TAG, "UI updating took %lld ms", elapsed_ms);

    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

// ====== main =======

void app_main(void) {
  ESP_LOGI(TAG, "hello, world!");

  // --- init vars

  m_state_mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(init_settings_storage());

  esp_err_t logger_init_err = dd_logger_init(&m_state, m_state_mutex);
  if (logger_init_err != ESP_OK) {
    ESP_LOGW(TAG, "Logger init failed, SD logging unavailable: %s", esp_err_to_name(logger_init_err));
  }

  // --- init UART

#ifndef CONFIG_DD_ENABLE_FAKE_DATA
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr_alloc_flags = 0;

  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 21, 22, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(
      uart_driver_install(UART_NUM_1, CONFIG_DD_UART_BUFFER_SIZE, CONFIG_DD_UART_BUFFER_SIZE, 10, &uart_queue, 0));
#endif

  // --- init audio

  ESP_ERROR_CHECK(bsp_extra_codec_init());
  ESP_ERROR_CHECK(bsp_extra_player_init());

  app_settings.brightness = DD_DEFAULT_BRIGHTNESS;
  app_settings.volume = clamp_int(bsp_extra_codec_volume_get(), 0, 100);
  ESP_ERROR_CHECK(load_app_settings(&app_settings));
  ESP_ERROR_CHECK(bsp_extra_codec_volume_set(app_settings.volume, NULL));

  // --- init display

  // anything that involves changing background opacity seems to benefit from full size buffer + spiram
  // otherwise not using spiram and smaller buffer is good
  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           //  .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
                           .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
                           //  .double_buffer = true,
                           .flags = {
                               .buff_dma = true,
                               //  .buff_spiram = false,
                               .buff_spiram = true,
                               .sw_rotate = false,
                           }};
  bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();
  bsp_display_brightness_set(app_settings.brightness);

  // --- init littlefs

  esp_vfs_littlefs_conf_t littlefs_conf = {
      .base_path = "/storage", .partition_label = NULL, .format_if_mount_failed = true};
  esp_err_t ret = esp_vfs_littlefs_register(&littlefs_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    return;
  }

  // --- init UI

  bsp_display_lock(0);

  dd_init_styles();

  lv_obj_t* splash_screen = lv_obj_create(NULL);
#if CONFIG_DD_ENABLE_INTRO_SPLASH
  lv_screen_load(splash_screen);
  dd_set_extremely_awesome_splash_screen(splash_screen);
#endif

  // setup screens

  overview_screen = lv_obj_create(NULL);
  dd_set_overview_screen(overview_screen, on_reset_button_clicked, on_options_button_clicked, on_log_button_clicked);

  metric_detail_screen = lv_obj_create(NULL);
  dd_set_metric_detail_screen(metric_detail_screen);

  options_screen = lv_obj_create(NULL);
  dd_set_options_screen(options_screen, &options_ui, app_settings.brightness, app_settings.volume);
  lv_obj_add_event_cb(options_ui.brightness_slider, on_options_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(options_ui.brightness_slider, on_options_slider_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(options_ui.volume_slider, on_options_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(options_ui.volume_slider, on_options_slider_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(options_ui.done_button, on_options_done_clicked, LV_EVENT_CLICKED, NULL);

  // lv_screen_load(options_screen);
  lv_screen_load(overview_screen);

  lv_obj_del(splash_screen);
  bsp_display_unlock();

  // --- everything else

  xTaskCreate(main_loop_task, "main_loop", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}
