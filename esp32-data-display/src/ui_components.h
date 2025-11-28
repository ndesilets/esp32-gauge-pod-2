#pragma once

#include <lvgl.h>
typedef struct {
  lv_obj_t* container;
  lv_obj_t* frame;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* main_value;
  lv_obj_t* minmax_value;
  int min_gauge_value;
  int max_gauge_value;
} framed_panel_t;

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title,
                                   const char* main_val, const char* minmax_val,
                                   int min_gauge_value, int max_gauge_value);

typedef struct {
  lv_obj_t* container;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* min_val;
  lv_obj_t* cur_val;
  lv_obj_t* max_val;
} simple_metric_t;

simple_metric_t simple_metric_create(lv_obj_t* parent, const char* title,
                                     const char* min_val, const char* cur_val,
                                     const char* max_val);

void dd_init_styles();
void dd_set_screen(lv_obj_t* obj);
void dd_set_flex_row(lv_obj_t* obj);
void dd_set_flex_column(lv_obj_t* obj);
