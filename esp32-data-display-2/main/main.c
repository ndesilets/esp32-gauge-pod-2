#include <stdbool.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "display_render_task.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "logger.h"
#include "monitoring.h"
#include "sdkconfig.h"
#include "ui_controller.h"
#include "uart_pipeline_task.h"

static const char* TAG = "app";

monitored_state_t m_state = {
    .water_temp = {.status = STATUS_OK},
    .oil_temp = {.status = STATUS_OK},
    .oil_pressure = {.status = STATUS_OK},
    .dam = {.current_value = 1.0, .status = STATUS_OK},
    .af_learned = {.current_value = 0.0, .status = STATUS_OK},
    .af_ratio = {.current_value = 14.7, .status = STATUS_OK},
    .int_temp = {.current_value = 67.0, .status = STATUS_OK},
    .fb_knock = {.current_value = 0.0, .status = STATUS_OK},
    .af_correct = {.current_value = 0.0, .status = STATUS_OK},
    .inj_duty = {.current_value = 0.0, .status = STATUS_OK},
    .eth_conc = {.current_value = 67.0, .status = STATUS_OK},
};

static SemaphoreHandle_t m_state_mutex;

void app_main(void) {
  ESP_LOGI(TAG, "hello, world!");

  m_state_mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(m_state_mutex ? ESP_OK : ESP_ERR_NO_MEM);

  dd_state_iface_t shared_state = {
      .state = &m_state,
      .mutex = m_state_mutex,
  };

  esp_err_t logger_init_err = dd_logger_init(&m_state, m_state_mutex);
  if (logger_init_err != ESP_OK) {
    ESP_LOGW(TAG, "Logger init failed, SD logging unavailable: %s", esp_err_to_name(logger_init_err));
  }

#ifndef CONFIG_DD_ENABLE_FAKE_DATA
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 21, 22, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(
      uart_driver_install(UART_NUM_1, CONFIG_DD_UART_BUFFER_SIZE, CONFIG_DD_UART_BUFFER_SIZE, 10, &uart_queue, 0));
#endif

  ESP_ERROR_CHECK(bsp_extra_codec_init());
  ESP_ERROR_CHECK(bsp_extra_player_init());

  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
                           .flags = {
                               .buff_dma = true,
                               .buff_spiram = true,
                               .sw_rotate = false,
                           }};
  bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();

  esp_vfs_littlefs_conf_t littlefs_conf = {
      .base_path = "/storage", .partition_label = NULL, .format_if_mount_failed = true};
  esp_err_t ret = esp_vfs_littlefs_register(&littlefs_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    return;
  }

  ESP_ERROR_CHECK(dd_ui_controller_init(&shared_state));
  ESP_ERROR_CHECK(dd_uart_pipeline_start(&shared_state));
  ESP_ERROR_CHECK(dd_display_render_start(&shared_state));
}
