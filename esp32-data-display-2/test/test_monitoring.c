#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "monitoring.h"

static monitored_state_t new_normal_state(void) {
  monitored_state_t state = {
      .water_temp = {.current_value = 190.0f, .status = STATUS_OK},
      .oil_temp = {.current_value = 200.0f, .status = STATUS_OK},
      .oil_pressure = {.current_value = 60.0f, .status = STATUS_OK},
      .dam = {.current_value = 1.0f, .status = STATUS_OK},
      .af_learned = {.current_value = 0.0f, .status = STATUS_OK},
      .fb_knock = {.current_value = 0.0f, .status = STATUS_OK},
      .inj_duty = {.current_value = 50.0f, .status = STATUS_OK},
  };
  return state;
}

static monitor_status oil_pressure_status(float oil_pressure_psi, unsigned int engine_rpm) {
  monitored_state_t state = new_normal_state();
  state.oil_pressure.current_value = oil_pressure_psi;
  evaluate_statuses(&state, engine_rpm);
  return state.oil_pressure.status;
}

static void test_oil_pressure_tracks_rpm_threshold(void) {
  assert(oil_pressure_status(0.0f, 299) == STATUS_NOT_READY);
  assert(oil_pressure_status(2.99f, 300) == STATUS_CRITICAL);
  assert(oil_pressure_status(3.0f, 300) == STATUS_OK);

  assert(oil_pressure_status(29.99f, 3000) == STATUS_CRITICAL);
  assert(oil_pressure_status(30.0f, 3000) == STATUS_OK);
}

static void test_oil_pressure_threshold_caps_at_sixty_psi(void) {
  assert(oil_pressure_status(59.99f, 6000) == STATUS_CRITICAL);
  assert(oil_pressure_status(60.0f, 6000) == STATUS_OK);
  assert(oil_pressure_status(59.99f, 7500) == STATUS_CRITICAL);
  assert(oil_pressure_status(60.0f, 7500) == STATUS_OK);
}

static void test_temperature_boundaries(void) {
  monitored_state_t state = new_normal_state();

  state.water_temp.current_value = 159.99f;
  evaluate_statuses(&state, 3000);
  assert(state.water_temp.status == STATUS_NOT_READY);
  state.water_temp.current_value = 160.0f;
  evaluate_statuses(&state, 3000);
  assert(state.water_temp.status == STATUS_OK);
  state.water_temp.current_value = 215.0f;
  evaluate_statuses(&state, 3000);
  assert(state.water_temp.status == STATUS_WARN);
  state.water_temp.current_value = 220.0f;
  evaluate_statuses(&state, 3000);
  assert(state.water_temp.status == STATUS_CRITICAL);

  state.oil_temp.current_value = 179.99f;
  evaluate_statuses(&state, 3000);
  assert(state.oil_temp.status == STATUS_NOT_READY);
  state.oil_temp.current_value = 180.0f;
  evaluate_statuses(&state, 3000);
  assert(state.oil_temp.status == STATUS_OK);
  state.oil_temp.current_value = 240.0f;
  evaluate_statuses(&state, 3000);
  assert(state.oil_temp.status == STATUS_WARN);
  state.oil_temp.current_value = 250.0f;
  evaluate_statuses(&state, 3000);
  assert(state.oil_temp.status == STATUS_CRITICAL);
}

static void test_dam_and_af_learning_boundaries(void) {
  monitored_state_t state = new_normal_state();

  state.dam.current_value = 0.999f;
  evaluate_statuses(&state, 3000);
  assert(state.dam.status == STATUS_CRITICAL);
  state.dam.current_value = 1.0f;
  evaluate_statuses(&state, 3000);
  assert(state.dam.status == STATUS_OK);

  state.af_learned.current_value = 10.99f;
  evaluate_statuses(&state, 3000);
  assert(state.af_learned.status == STATUS_OK);
  state.af_learned.current_value = 11.0f;
  evaluate_statuses(&state, 3000);
  assert(state.af_learned.status == STATUS_WARN);
  state.af_learned.current_value = -11.0f;
  evaluate_statuses(&state, 3000);
  assert(state.af_learned.status == STATUS_WARN);
}

static void test_feedback_knock_boundaries(void) {
  monitored_state_t state = new_normal_state();

  state.fb_knock.current_value = -2.82f;
  evaluate_statuses(&state, 2000);
  assert(state.fb_knock.status == STATUS_CRITICAL);
  state.fb_knock.current_value = -2.81f;
  evaluate_statuses(&state, 2000);
  assert(state.fb_knock.status == STATUS_WARN);
  state.fb_knock.current_value = -0.01f;
  evaluate_statuses(&state, 2000);
  assert(state.fb_knock.status == STATUS_WARN);
  state.fb_knock.current_value = 0.0f;
  evaluate_statuses(&state, 2000);
  assert(state.fb_knock.status == STATUS_OK);
}

static void test_low_rpm_knock_preserves_previous_status(void) {
  monitored_state_t state = new_normal_state();
  state.fb_knock.current_value = 0.0f;
  state.fb_knock.status = STATUS_WARN;

  evaluate_statuses(&state, 1999);
  assert(state.fb_knock.status == STATUS_WARN);
}

static void test_injector_duty_boundaries(void) {
  monitored_state_t state = new_normal_state();

  state.inj_duty.current_value = 90.0f;
  evaluate_statuses(&state, 3000);
  assert(state.inj_duty.status == STATUS_OK);
  state.inj_duty.current_value = 90.01f;
  evaluate_statuses(&state, 3000);
  assert(state.inj_duty.status == STATUS_WARN);
  state.inj_duty.current_value = 99.99f;
  evaluate_statuses(&state, 3000);
  assert(state.inj_duty.status == STATUS_WARN);
  state.inj_duty.current_value = 100.0f;
  evaluate_statuses(&state, 3000);
  assert(state.inj_duty.status == STATUS_CRITICAL);
}

static void test_alert_transitions_only_fire_for_new_or_escalated_alerts(void) {
  monitored_state_t previous = new_normal_state();
  monitored_state_t current = previous;

  current.oil_temp.status = STATUS_WARN;
  assert(has_alert_transition(&previous, &current));

  previous.oil_temp.status = STATUS_WARN;
  current.oil_temp.status = STATUS_CRITICAL;
  assert(has_alert_transition(&previous, &current));

  previous.oil_temp.status = STATUS_CRITICAL;
  current.oil_temp.status = STATUS_WARN;
  assert(!has_alert_transition(&previous, &current));

  previous.oil_temp.status = STATUS_WARN;
  current.oil_temp.status = STATUS_OK;
  assert(!has_alert_transition(&previous, &current));

  previous.oil_temp.status = STATUS_OK;
  current.oil_temp.status = STATUS_OK;
  assert(!has_alert_transition(&previous, &current));
}

static void test_numeric_monitor_tracks_extrema(void) {
  numeric_monitor_t monitor = {
      .current_value = 10.0f,
      .min_value = 10.0f,
      .max_value = 10.0f,
  };

  update_numeric_monitor(&monitor, 8.0f);
  assert(monitor.current_value == 8.0f);
  assert(monitor.min_value == 8.0f);
  assert(monitor.max_value == 10.0f);

  update_numeric_monitor(&monitor, 12.0f);
  assert(monitor.current_value == 12.0f);
  assert(monitor.min_value == 8.0f);
  assert(monitor.max_value == 12.0f);
}

static void test_reset_monitored_state_resets_every_numeric_field(void) {
  monitored_state_t state = {0};
  numeric_monitor_t* monitors[] = {
      &state.water_temp, &state.oil_temp,   &state.oil_pressure, &state.dam,
      &state.af_learned, &state.af_ratio,   &state.int_temp,     &state.fb_knock,
      &state.af_correct, &state.inj_duty,   &state.eth_conc,
  };

  for (size_t i = 0; i < sizeof(monitors) / sizeof(monitors[0]); ++i) {
    monitors[i]->current_value = (float)i + 1.0f;
    monitors[i]->min_value = -100.0f;
    monitors[i]->max_value = 100.0f;
  }

  reset_monitored_state(&state);

  for (size_t i = 0; i < sizeof(monitors) / sizeof(monitors[0]); ++i) {
    assert(monitors[i]->min_value == monitors[i]->current_value);
    assert(monitors[i]->max_value == monitors[i]->current_value);
  }
}

int main(void) {
  test_oil_pressure_tracks_rpm_threshold();
  test_oil_pressure_threshold_caps_at_sixty_psi();
  test_temperature_boundaries();
  test_dam_and_af_learning_boundaries();
  test_feedback_knock_boundaries();
  test_low_rpm_knock_preserves_previous_status();
  test_injector_duty_boundaries();
  test_alert_transitions_only_fire_for_new_or_escalated_alerts();
  test_numeric_monitor_tracks_extrema();
  test_reset_monitored_state_resets_every_numeric_field();
  puts("display monitoring tests passed");
  return 0;
}
