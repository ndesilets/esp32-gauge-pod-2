#include "car_data.h"

#include "sdkconfig.h"

#ifdef CONFIG_DD_ENABLE_FAKE_DATA
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

telemetry_packet_t get_data() {
  static int i = 0;

  telemetry_packet_t packet = {
      // metadata
      .sequence = i,
      .timestamp_ms = esp_timer_get_time() / 1000,

      // primary
      .water_temp = wrap_range(i / 4, 190, 240),
      .oil_temp = wrap_range(i / 4, 200, 300),
      .oil_pressure = wrap_range(i / 4, 0, 100),

      .dam = map_sine_to_range(sinf(i / 50.0f), 0, 1.049),
      .af_learned = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      .af_ratio = map_sine_to_range(sinf(i / 50.0f), 10.0, 20.0),
      .int_temp = map_sine_to_range(sinf(i / 50.0f), 0, 120),

      .fb_knock = map_sine_to_range(sinf(i / 50.0f), -6, 0.49),
      .af_correct = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      .inj_duty = map_sine_to_range(sinf(i / 50.0f), 0, 105),
      .eth_conc = map_sine_to_range(sinf(i / 50.0f), 10, 85),

      // supplemental
      .engine_rpm = 2500.0f,
  };

  i++;

  return packet;
}
#else
telemetry_packet_t get_data() {
  telemetry_packet_t packet = {
      // metadata
      .sequence = 0,
      .timestamp_ms = 0,

      // primary
      .water_temp = 0.0f,
      .oil_temp = 0.0f,
      .oil_pressure = 0.0f,

      .dam = 0.0f,
      .af_learned = 0.0f,
      .af_ratio = 0.0f,
      .int_temp = 0.0f,

      .fb_knock = 0.0f,
      .af_correct = 0.0f,
      .inj_duty = 0.0f,
      .eth_conc = 0.0f,

      // supplemental
      .engine_rpm = 0.0f,
  };

  return packet;
}
#endif