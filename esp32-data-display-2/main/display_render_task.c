#include "display_render_task.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_controller.h"

static const char* TAG = "display_render";

static dd_state_iface_t s_state_iface = {0};
static TaskHandle_t s_render_task = NULL;

static void display_render_task(void* arg) {
  for (;;) {
    monitored_state_t snapshot = {0};
    bool have_snapshot = false;

    if (xSemaphoreTake(s_state_iface.mutex, pdMS_TO_TICKS(100))) {
      snapshot = *s_state_iface.state;
      have_snapshot = true;
      xSemaphoreGive(s_state_iface.mutex);
    } else {
      ESP_LOGW(TAG, "Could not take m_state mutex in render task");
    }

    if (have_snapshot && bsp_display_lock(pdMS_TO_TICKS(100))) {
      dd_ui_controller_render(&snapshot);
      bsp_display_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

esp_err_t dd_display_render_start(const dd_state_iface_t* shared_state) {
  ESP_RETURN_ON_FALSE(shared_state, ESP_ERR_INVALID_ARG, TAG, "shared_state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->state, ESP_ERR_INVALID_ARG, TAG, "shared_state->state is NULL");
  ESP_RETURN_ON_FALSE(shared_state->mutex, ESP_ERR_INVALID_ARG, TAG, "shared_state->mutex is NULL");
  ESP_RETURN_ON_FALSE(!s_render_task, ESP_ERR_INVALID_STATE, TAG, "display render task already started");

  s_state_iface = *shared_state;
  BaseType_t ok = xTaskCreate(display_render_task, "display_render", 4096, NULL, tskIDLE_PRIORITY + 1, &s_render_task);
  return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}
