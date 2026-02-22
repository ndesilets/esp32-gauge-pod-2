#include "lvgl.h"
#include "monitoring.h"

typedef struct {
  lv_obj_t* container;
  lv_obj_t* frame;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* main_value;
  lv_obj_t* minmax_value;
  lv_obj_t* bar;
} framed_panel_t;

typedef struct {
  lv_obj_t* container;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* min_val;
  lv_obj_t* cur_val;
  lv_obj_t* max_val;
} simple_metric_t;

typedef struct {
  lv_obj_t* brightness_slider;
  lv_obj_t* brightness_value_label;
  lv_obj_t* volume_slider;
  lv_obj_t* volume_value_label;
  lv_obj_t* done_button;
} dd_options_screen_t;

// --- general purpose stuff

void dd_init_styles();
void dd_set_screen(lv_obj_t* obj);
void dd_set_flex_row(lv_obj_t* obj);
void dd_set_flex_column(lv_obj_t* obj);

// --- ui components

void dd_set_simple_metric_column(lv_obj_t* col);
void dd_set_framed_controls_row(lv_obj_t* obj);
void dd_set_action_button(lv_obj_t* btn, const char* label);

// --- overview screen components

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title, int cur_val, int min_bar_value,
                                   int max_bar_value);
void framed_panel_update(framed_panel_t* panel, int cur_val, int min_val, int max_val, monitor_status status);

simple_metric_t simple_metric_create(lv_obj_t* parent, const char* title, float cur_val);
void simple_metric_update(simple_metric_t* metric, float cur_val, float min_val, float max_val, monitor_status status);

// --- screens

// splash

void dd_set_extremely_awesome_splash_screen(lv_obj_t* screen);

// overview

void dd_set_overview_screen(lv_obj_t* screen, lv_event_cb_t on_reset_button_clicked,
                            lv_event_cb_t on_options_button_clicked, lv_event_cb_t on_record_button_clicked);
void dd_update_overview_screen(monitored_state_t m_state);

// metric detail

void dd_set_metric_detail_screen(lv_obj_t* screen);
void dd_update_metric_detail_screen(monitored_state_t m_state);

// options

void dd_set_options_screen(lv_obj_t* screen, dd_options_screen_t* out, int initial_brightness, int initial_volume);
