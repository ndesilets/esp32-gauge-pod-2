#include "task_twai_monitor.h"

#include <inttypes.h>

#include "app_context.h"
#include "esp_log.h"

static const char* TAG = "task_twai_monitor";

void task_twai_monitor(void* arg) {
  app_context_t* app = (app_context_t*)arg;
  if (app == NULL || app->node_hdl == NULL) {
    vTaskDelete(NULL);
    return;
  }

  twai_node_status_t last_status = {0};
  twai_node_record_t last_record = {0};
  bool has_last = false;

  while (1) {
    twai_node_status_t status = {0};
    twai_node_record_t record = {0};
    if (twai_node_get_info(app->node_hdl, &status, &record) == ESP_OK) {
      if (!has_last || status.state != last_status.state || status.tx_error_count != last_status.tx_error_count ||
          status.rx_error_count != last_status.rx_error_count || record.bus_err_num != last_record.bus_err_num) {
        ESP_LOGW(TAG, "TWAI status state=%d tx_err=%u rx_err=%u bus_err=%" PRIu32, status.state, status.tx_error_count,
                 status.rx_error_count, record.bus_err_num);
        last_status = status;
        last_record = record;
        has_last = true;
      }
    } else {
      ESP_LOGW(TAG, "TWAI status read failed");
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
