#include "car_data.h"

#include <stdbool.h>
#include <string.h>

#include "esp_timer.h"
#include "math.h"
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

bool get_data(vehicle_state_t* packet) {
  if (!packet) {
    return false;
  }

  static int i = 0;

  *packet = (vehicle_state_t){
      // metadata
      .sequence = i,
      .timestamp_ms = esp_timer_get_time() / 1000,

      // primary
      // .water_temp = wrap_range(i / 4, 190, 240),
      .oil_temp = wrap_range(i / 10, 200, 300),
      // .oil_pressure = wrap_range(i / 4, 0, 100),

      // .dam = map_sine_to_range(sinf(i / 50.0f), 0, 1.049),
      // .af_learned = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      // .af_ratio = map_sine_to_range(sinf(i / 50.0f), 10.0, 20.0),
      // .int_temp = map_sine_to_range(sinf(i / 50.0f), 0, 120),

      // .fb_knock = map_sine_to_range(sinf(i / 50.0f), -6, 0.49),
      // .af_correct = map_sine_to_range(sinf(i / 50.0f), -10, 10),
      // .inj_duty = map_sine_to_range(sinf(i / 50.0f), 0, 105),
      // .eth_conc = map_sine_to_range(sinf(i / 50.0f), 10, 85),

      // supplemental
      .engine_rpm = 2500.0f,
  };

  i++;

  return true;
}

void dd_car_data_uart_resync(void) {}
#else
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "telemetry_protocol.h"

static const char* TAG = "car_data";
static const TickType_t UART_PARTIAL_FRAME_TIMEOUT_TICKS = pdMS_TO_TICKS(100);

static uint8_t s_uart_rx_buf[CONFIG_DD_UART_BUFFER_SIZE];
static size_t s_uart_rx_len = 0;
static TickType_t s_uart_last_rx_tick = 0;

static void drop_consumed_bytes(size_t consumed) {
  if (consumed >= s_uart_rx_len) {
    s_uart_rx_len = 0;
    return;
  }

  memmove(s_uart_rx_buf, s_uart_rx_buf + consumed, s_uart_rx_len - consumed);
  s_uart_rx_len -= consumed;
}

static int find_frame_delimiter(void) {
  for (size_t i = 0; i < s_uart_rx_len; i++) {
    if (s_uart_rx_buf[i] == 0x00) {
      return (int)i;
    }
  }
  return -1;
}

void dd_car_data_uart_resync(void) {
  uart_flush_input(UART_NUM_1);
  s_uart_rx_len = 0;
  s_uart_last_rx_tick = xTaskGetTickCount();
}

bool get_data(vehicle_state_t* packet) {
  if (!packet) {
    return false;
  }

  int bytes_read = 0;
  if (s_uart_rx_len < sizeof(s_uart_rx_buf)) {
    bytes_read = uart_read_bytes(UART_NUM_1, s_uart_rx_buf + s_uart_rx_len, sizeof(s_uart_rx_buf) - s_uart_rx_len,
                                 pdMS_TO_TICKS(10));
    if (bytes_read > 0) {
      s_uart_rx_len += (size_t)bytes_read;
      s_uart_last_rx_tick = xTaskGetTickCount();
    }
  }

  while (1) {
    int delimiter_idx = find_frame_delimiter();
    if (delimiter_idx < 0) {
      break;
    }

    const size_t frame_len = (size_t)delimiter_idx;
    if (frame_len == 0) {
      drop_consumed_bytes(1);
      continue;
    }

    const telemetry_result_t result = telemetry_frame_decode(s_uart_rx_buf, frame_len, packet);
    const bool decoded = result == TELEMETRY_RESULT_OK;
    if (!decoded) {
      ESP_LOGW(TAG, "telemetry frame rejected: %s (len=%u)", telemetry_result_name(result),
               (unsigned)frame_len);
    }

    drop_consumed_bytes(frame_len + 1);
    if (decoded) {
      return true;
    }
  }

  if (s_uart_rx_len >= sizeof(s_uart_rx_buf)) {
    ESP_LOGW(TAG, "UART RX buffer filled without frame delimiter, forcing resync");
    dd_car_data_uart_resync();
    return false;
  }

  if (s_uart_rx_len > 0 && s_uart_last_rx_tick != 0 &&
      (xTaskGetTickCount() - s_uart_last_rx_tick) >= UART_PARTIAL_FRAME_TIMEOUT_TICKS) {
    ESP_LOGW(TAG, "discarding stalled partial UART frame (%u bytes)", (unsigned)s_uart_rx_len);
    s_uart_rx_len = 0;
  }

  return false;
}
#endif
