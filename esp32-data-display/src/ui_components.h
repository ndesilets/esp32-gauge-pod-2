#pragma once

#include <lvgl.h>
typedef struct {
  lv_obj_t* container;
  lv_obj_t* frame;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* main_value;
  lv_obj_t* minmax_value;
  lv_obj_t* bar;
  int min_value;
  int max_value;
} framed_panel_t;

framed_panel_t framed_panel_create(lv_obj_t* parent, const char* title,
                                   int cur_val, int min_bar_value,
                                   int max_bar_value);
void framed_panel_update(framed_panel_t* panel, int cur_val);

typedef struct {
  lv_obj_t* container;
  lv_obj_t* title;
  lv_obj_t* body;
  lv_obj_t* min_val;
  lv_obj_t* cur_val;
  lv_obj_t* max_val;
  float min_value;
  float max_value;
} simple_metric_t;

simple_metric_t simple_metric_create(lv_obj_t* parent, const char* title,
                                     float cur_val);
void simple_metric_update(simple_metric_t* metric, float cur_val);

void dd_init_styles();
void dd_set_screen(lv_obj_t* obj);
void dd_set_flex_row(lv_obj_t* obj);
void dd_set_flex_column(lv_obj_t* obj);
