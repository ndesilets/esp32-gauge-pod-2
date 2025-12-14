#pragma once

#include <stdbool.h>

// info = something you might want to look at, like low knock value at lower load
// warn = something you should start paying attention to, like when oil hits 240F
// critical = something you should take action on immediately, like when coolant hits 220F
typedef enum { STATUS_NOT_READY, STATUS_OK, STATUS_INFO, STATUS_WARN, STATUS_CRITICAL } monitor_status;

typedef struct {
  float current_value;
  float min_value;
  float max_value;
  monitor_status status;
} numeric_monitor_t;

typedef struct {
  numeric_monitor_t water_temp;
  numeric_monitor_t oil_temp;
  numeric_monitor_t oil_pressure;

  numeric_monitor_t dam;
  numeric_monitor_t af_learned;
  numeric_monitor_t af_ratio;
  numeric_monitor_t int_temp;

  numeric_monitor_t fb_knock;
  numeric_monitor_t af_correct;
  numeric_monitor_t inj_duty;
  numeric_monitor_t eth_conc;
} monitored_state_t;

void update_numeric_monitor(numeric_monitor_t* monitor, float new_value);

// compares states to check for any newly set warn/critical status transitions
bool has_alert_transition(const monitored_state_t* prev, const monitored_state_t* curr);

// check m_state and set status fields appropriately
void evaluate_statuses(monitored_state_t* m_state, unsigned int engine_rpm);
