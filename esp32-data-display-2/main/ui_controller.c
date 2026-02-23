#include "ui_controller.h"

#include <stdbool.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "logger.h"
#include "lvgl.h"
#include "monitoring.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_components.h"

static const char* TAG = "ui_controller";

typedef enum { OVERVIEW, METRIC_DETAIL, OPTIONS } ui_state_t;

static dd_state_iface_t s_state_iface = {0};
static ui_state_t s_ui_state = OVERVIEW;

static lv_obj_t* s_overview_screen;
static lv_obj_t* s_metric_detail_screen;
static lv_obj_t* s_options_screen;
static dd_options_screen_t s_options_ui;

static lv_obj_t* s_log_button_obj = NULL;
static bool s_log_button_is_active = false;

#define DD_SETTINGS_NAMESPACE "dd_settings"
#define DD_SETTINGS_KEY_BRIGHTNESS "brightness"
#define DD_SETTINGS_KEY_VOLUME "volume"
#define DD_DEFAULT_BRIGHTNESS 50

typedef struct {
  int brightness;
  int volume;
} app_settings_t;

static app_settings_t s_app_settings = {
    .brightness = DD_DEFAULT_BRIGHTNESS,
    .volume = 0,
};

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
  if (!s_options_ui.brightness_value_label || !s_options_ui.volume_value_label) {
    return;
  }

  lv_label_set_text_fmt(s_options_ui.brightness_value_label, "%d%%", brightness_to_percent(s_app_settings.brightness));
  lv_label_set_text_fmt(s_options_ui.volume_value_label, "%d%%", s_app_settings.volume);
}

static void load_overview_screen(void) {
  s_ui_state = OVERVIEW;
  lv_screen_load(s_overview_screen);
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

  s_log_button_is_active = active;
}

static void on_reset_button_clicked(lv_event_t* e) {
  if (xSemaphoreTake(s_state_iface.mutex, pdMS_TO_TICKS(100))) {
    reset_monitored_state(s_state_iface.state);
    xSemaphoreGive(s_state_iface.mutex);
  }
}

static void on_options_button_clicked(lv_event_t* e) {
  ESP_LOGI(TAG, "Options button clicked");
  s_ui_state = OPTIONS;
  lv_screen_load(s_options_screen);
}

static void on_log_button_clicked(lv_event_t* e) {
  s_log_button_obj = lv_event_get_target(e);

  if (dd_logger_is_active()) {
    ESP_LOGI(TAG, "Stopping logger");
    dd_logger_stop();
    set_log_button_active_style(s_log_button_obj, false);
    return;
  }

  esp_err_t err = dd_logger_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start logger: %s", esp_err_to_name(err));
    set_log_button_active_style(s_log_button_obj, false);
    return;
  }

  ESP_LOGI(TAG, "Logger started");
  set_log_button_active_style(s_log_button_obj, true);
}

static void on_options_slider_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* target = lv_event_get_target(e);

  if (target == s_options_ui.brightness_slider) {
    s_app_settings.brightness = clamp_int(lv_slider_get_value(s_options_ui.brightness_slider),
                                          BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN, BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX);
    bsp_display_brightness_set(s_app_settings.brightness);
  } else if (target == s_options_ui.volume_slider) {
    s_app_settings.volume = clamp_int(lv_slider_get_value(s_options_ui.volume_slider), 0, 100);
    esp_err_t volume_err = bsp_extra_codec_volume_set(s_app_settings.volume, NULL);
    if (volume_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set volume: %s", esp_err_to_name(volume_err));
    }
  }

  update_options_value_labels();

  if (code == LV_EVENT_RELEASED) {
    esp_err_t save_err = save_app_settings(&s_app_settings);
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

esp_err_t dd_ui_controller_init(const dd_state_iface_t* shared_state) {
  ESP_RETURN_ON_FALSE(shared_state, ESP_ERR_INVALID_ARG, TAG, "shared_state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->state, ESP_ERR_INVALID_ARG, TAG, "shared_state->state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->mutex, ESP_ERR_INVALID_ARG, TAG, "shared_state->mutex is NULL");

  s_state_iface = *shared_state;

  ESP_RETURN_ON_ERROR(init_settings_storage(), TAG, "Failed to init settings storage");

  s_app_settings.brightness = DD_DEFAULT_BRIGHTNESS;
  s_app_settings.volume = clamp_int(bsp_extra_codec_volume_get(), 0, 100);
  ESP_RETURN_ON_ERROR(load_app_settings(&s_app_settings), TAG, "Failed to load app settings");
  ESP_RETURN_ON_ERROR(bsp_extra_codec_volume_set(s_app_settings.volume, NULL), TAG, "Failed to apply volume");
  bsp_display_brightness_set(s_app_settings.brightness);

  bsp_display_lock(0);

  dd_init_styles();

  lv_obj_t* splash_screen = lv_obj_create(NULL);
#if CONFIG_DD_ENABLE_INTRO_SPLASH
  lv_screen_load(splash_screen);
  dd_set_extremely_awesome_splash_screen(splash_screen);
#endif

  s_overview_screen = lv_obj_create(NULL);
  dd_set_overview_screen(s_overview_screen, on_reset_button_clicked, on_options_button_clicked, on_log_button_clicked);

  s_metric_detail_screen = lv_obj_create(NULL);
  dd_set_metric_detail_screen(s_metric_detail_screen);

  s_options_screen = lv_obj_create(NULL);
  dd_set_options_screen(s_options_screen, &s_options_ui, s_app_settings.brightness, s_app_settings.volume);
  lv_obj_add_event_cb(s_options_ui.brightness_slider, on_options_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(s_options_ui.brightness_slider, on_options_slider_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(s_options_ui.volume_slider, on_options_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(s_options_ui.volume_slider, on_options_slider_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(s_options_ui.done_button, on_options_done_clicked, LV_EVENT_CLICKED, NULL);

  lv_screen_load(s_overview_screen);
  s_ui_state = OVERVIEW;

  lv_obj_del(splash_screen);
  bsp_display_unlock();

  return ESP_OK;
}

void dd_ui_controller_render(const monitored_state_t* snapshot) {
  if (!snapshot) {
    return;
  }

  if (s_log_button_obj && s_log_button_is_active && !dd_logger_is_active()) {
    set_log_button_active_style(s_log_button_obj, false);
  }

  switch (s_ui_state) {
    case OVERVIEW:
      dd_update_overview_screen(*snapshot);
      break;
    case METRIC_DETAIL:
      dd_update_metric_detail_screen(*snapshot);
      break;
    case OPTIONS:
      break;
    default:
      break;
  }
}

