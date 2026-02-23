#include "uart_pipeline_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_board_extra.h"
#include "car_data.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitoring.h"

static const char* TAG = "uart_pipeline";

static dd_state_iface_t s_state_iface = {0};
static TaskHandle_t s_uart_task = NULL;

static void uart_pipeline_task(void* arg) {
  monitored_state_t prev_state = {0};
  bool prev_state_valid = false;

  for (;;) {
    display_packet_t packet = {0};
    bool received = get_data(&packet);
    if (!received) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool alert_transition = false;
    if (xSemaphoreTake(s_state_iface.mutex, pdMS_TO_TICKS(100))) {
      update_numeric_monitor(&s_state_iface.state->water_temp, packet.water_temp);
      update_numeric_monitor(&s_state_iface.state->oil_temp, packet.oil_temp);
      update_numeric_monitor(&s_state_iface.state->oil_pressure, packet.oil_pressure);

      update_numeric_monitor(&s_state_iface.state->dam, packet.dam);
      update_numeric_monitor(&s_state_iface.state->af_learned, packet.af_learned);
      update_numeric_monitor(&s_state_iface.state->af_ratio, packet.af_ratio);
      update_numeric_monitor(&s_state_iface.state->int_temp, packet.int_temp);

      update_numeric_monitor(&s_state_iface.state->fb_knock, packet.fb_knock);
      update_numeric_monitor(&s_state_iface.state->af_correct, packet.af_correct);
      update_numeric_monitor(&s_state_iface.state->inj_duty, packet.inj_duty);
      update_numeric_monitor(&s_state_iface.state->eth_conc, packet.eth_conc);

      evaluate_statuses(s_state_iface.state, packet.engine_rpm);

      if (prev_state_valid) {
        alert_transition = has_alert_transition(&prev_state, s_state_iface.state);
      }

      prev_state = *s_state_iface.state;
      prev_state_valid = true;

      xSemaphoreGive(s_state_iface.mutex);
    } else {
      ESP_LOGW(TAG, "Could not take m_state mutex in uart pipeline task");
    }

    if (alert_transition) {
#ifdef CONFIG_DD_ENABLE_ALERT_AUDIO
      ESP_ERROR_CHECK(bsp_extra_player_play_file("/storage/audio/tacobell.wav"));
#endif
      ESP_LOGW(TAG, "uh oh stinky");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t dd_uart_pipeline_start(const dd_state_iface_t* shared_state) {
  ESP_RETURN_ON_FALSE(shared_state, ESP_ERR_INVALID_ARG, TAG, "shared_state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->state, ESP_ERR_INVALID_ARG, TAG, "shared_state->state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->mutex, ESP_ERR_INVALID_ARG, TAG, "shared_state->mutex is NULL");
  ESP_RETURN_ON_FALSE(!s_uart_task, ESP_ERR_INVALID_STATE, TAG, "UART pipeline task already started");

  s_state_iface = *shared_state;
  BaseType_t ok = xTaskCreate(uart_pipeline_task, "uart_pipeline", 4096, NULL, tskIDLE_PRIORITY + 2, &s_uart_task);
  return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

