#include "monitoring.h"

#include "math.h"

bool is_alert_status(monitor_status status) { return status == STATUS_WARN || status == STATUS_CRITICAL; }

bool is_new_alert(monitor_status previous, monitor_status current) {
  if (!is_alert_status(current)) {
    return false;
  }
  // New alert if we were not alerting before, or if severity increased (WARN -> CRITICAL).
  if (!is_alert_status(previous)) {
    return true;
  }
  return previous == STATUS_WARN && current == STATUS_CRITICAL;
}

bool has_alert_transition(const monitored_state_t* prev, const monitored_state_t* curr) {
  return is_new_alert(prev->water_temp.status, curr->water_temp.status) ||
         is_new_alert(prev->oil_temp.status, curr->oil_temp.status) ||
         is_new_alert(prev->oil_pressure.status, curr->oil_pressure.status) ||
         is_new_alert(prev->dam.status, curr->dam.status) ||
         is_new_alert(prev->af_learned.status, curr->af_learned.status) ||
         is_new_alert(prev->af_ratio.status, curr->af_ratio.status) ||
         is_new_alert(prev->int_temp.status, curr->int_temp.status) ||
         is_new_alert(prev->fb_knock.status, curr->fb_knock.status) ||
         is_new_alert(prev->af_correct.status, curr->af_correct.status) ||
         is_new_alert(prev->inj_duty.status, curr->inj_duty.status) ||
         is_new_alert(prev->eth_conc.status, curr->eth_conc.status);
}

void evaluate_statuses(monitored_state_t m_state, unsigned int engine_rpm) {
  // --- do monitoring logic

  if (m_state.water_temp.current_value < 160) {
    m_state.water_temp.status = STATUS_NOT_READY;
  } else if (m_state.water_temp.current_value < 215) {
    m_state.water_temp.status = STATUS_OK;
  } else if (m_state.water_temp.current_value < 220) {
    m_state.water_temp.status = STATUS_WARN;
  } else {
    m_state.water_temp.status = STATUS_CRITICAL;
  }

  if (m_state.oil_temp.current_value < 180) {
    m_state.oil_temp.status = STATUS_NOT_READY;
  } else if (m_state.oil_temp.current_value < 240) {
    m_state.oil_temp.status = STATUS_OK;
  } else if (m_state.oil_temp.current_value < 250) {
    m_state.oil_temp.status = STATUS_WARN;
  } else {
    m_state.oil_temp.status = STATUS_CRITICAL;
  }

  // TODO: oil pressure alarm
  // needs to be correlated with engine RPM within +- some range

  if (m_state.dam.current_value < 1.0) {
    m_state.dam.status = STATUS_CRITICAL;
  } else {
    m_state.dam.status = STATUS_OK;
  }

  if (fabsf(m_state.af_learned.current_value) >= 11) {
    m_state.af_learned.status = STATUS_WARN;
  } else {
    m_state.af_learned.status = STATUS_OK;
  }

  // TODO: monitor AFR with secondary wideband

  if (engine_rpm >= 2000) {  // anything below is likely noise
    if (m_state.fb_knock.current_value < -2.81) {
      m_state.fb_knock.status = STATUS_CRITICAL;
    } else if (m_state.fb_knock.current_value < 0) {
      m_state.fb_knock.status = STATUS_WARN;
    } else {
      m_state.fb_knock.status = STATUS_OK;
    }
  }

  if (m_state.inj_duty.current_value > 90) {
    m_state.inj_duty.status = STATUS_WARN;
  } else if (m_state.inj_duty.current_value >= 100) {
    m_state.inj_duty.status = STATUS_CRITICAL;
  } else {
    m_state.inj_duty.status = STATUS_OK;
  }
}