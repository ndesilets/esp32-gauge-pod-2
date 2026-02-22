/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>

#include "app_context.h"
#include "can_transport.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "task_analog_data.h"
#include "task_can_rx_dispatcher.h"
#include "task_car_data.h"
#include "task_twai_monitor.h"
#include "task_uart_emitter.h"

static const char* TAG = "data_hub";
#define DH_UART_PORT ((uart_port_t)CONFIG_DH_UART_PORT)

void app_main(void) {
  printf("Hello world!\n");

  twai_onchip_node_config_t node_config = {
      .io_cfg.tx = CONFIG_DH_TWAI_TX_GPIO,
      .io_cfg.rx = CONFIG_DH_TWAI_RX_GPIO,
      .bit_timing.bitrate = 500000,
      .tx_queue_depth = 1,  // 8 should be plenty for ecu responses (largest one)
  };
  twai_node_handle_t node_hdl = NULL;
  ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

  static app_context_t app = {0};
  if (!app_context_init(&app, node_hdl)) {
    ESP_LOGE(TAG, "Failed to create app context");
    return;
  }

  twai_event_callbacks_t twai_cbs = {
      .on_rx_done = can_transport_rx_callback,
  };
  ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &twai_cbs, &app));
  ESP_ERROR_CHECK(twai_node_enable(node_hdl));

#ifdef CONFIG_DH_UART_ENABLED
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr_alloc_flags = 0;

  ESP_ERROR_CHECK(uart_param_config(DH_UART_PORT, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(DH_UART_PORT, CONFIG_DH_UART_TX_GPIO, CONFIG_DH_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  QueueHandle_t uart_queue;
  ESP_ERROR_CHECK(uart_driver_install(DH_UART_PORT, CONFIG_DH_UART_BUFFER_SIZE, CONFIG_DH_UART_BUFFER_SIZE, 10,
                                      &uart_queue, intr_alloc_flags));
#endif

  xTaskCreate(task_car_data, "task_car_data", 8192 * 2, (void*)&app, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(task_can_rx_dispatcher, "task_can_rx_dispatcher", 8192, (void*)&app, tskIDLE_PRIORITY + 2, NULL);
  xTaskCreate(task_analog_data, "task_analog_data", 8192, (void*)&app, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreate(task_twai_monitor, "task_twai_monitor", 4096, (void*)&app, tskIDLE_PRIORITY + 1, NULL);
#ifdef CONFIG_DH_UART_ENABLED
  xTaskCreate(task_uart_emitter, "task_uart_emitter", 8192, (void*)&app, tskIDLE_PRIORITY + 1, NULL);
#endif
}
