#include <stdbool.h>
#include <stdint.h>

#include "analog_sensors_backend.h"
#include "analog_sensors_math.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "analog_ads1115";

static const float k_v_fsr = 6.144f;
static const float k_v_sup = 4.96f;
static const float k_bias_ohms = 3000.0f;

static bool s_i2c_initialized = false;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_ads1115 = NULL;

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 0x01

#define ADS1115_OS_SINGLE (1u << 15)
#define ADS1115_MUX_AIN0_GND (0x4u << 12)
#define ADS1115_MUX_AIN1_GND (0x5u << 12)
#define ADS1115_MUX_AIN2_GND (0x6u << 12)
#define ADS1115_MUX_AIN3_GND (0x7u << 12)
#define ADS1115_PGA_6_144V (0x0u << 9)
#define ADS1115_MODE_SINGLE (1u << 8)
#define ADS1115_DR_128SPS (0x4u << 5)
#define ADS1115_COMP_DISABLE (0x3u)

static esp_err_t ads1115_write_reg(uint8_t reg, uint16_t value) {
  uint8_t payload[3] = {
      reg,
      (uint8_t)((value >> 8) & 0xFF),
      (uint8_t)(value & 0xFF),
  };

  return i2c_master_transmit(s_ads1115, payload, sizeof(payload), 50);
}

static esp_err_t ads1115_read_reg(uint8_t reg, uint16_t* out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t data[2] = {0};
  esp_err_t err = i2c_master_transmit_receive(s_ads1115, &reg, sizeof(reg), data, sizeof(data), 50);
  if (err != ESP_OK) {
    return err;
  }

  *out = (uint16_t)((data[0] << 8) | data[1]);
  return ESP_OK;
}

static esp_err_t ads1115_read_single_ended_raw(uint16_t mux_bits, int16_t* out_raw) {
  if (out_raw == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint16_t cfg = ADS1115_OS_SINGLE | mux_bits | ADS1115_PGA_6_144V | ADS1115_MODE_SINGLE | ADS1115_DR_128SPS |
                       ADS1115_COMP_DISABLE;

  esp_err_t err = ads1115_write_reg(ADS1115_REG_CONFIG, cfg);
  if (err != ESP_OK) {
    return err;
  }

  // At 128 SPS, conversion time is ~7.8ms. Give margin before reading conversion register.
  vTaskDelay(pdMS_TO_TICKS(10));

  uint16_t conv = 0;
  err = ads1115_read_reg(ADS1115_REG_CONVERSION, &conv);
  if (err != ESP_OK) {
    return err;
  }

  *out_raw = (int16_t)conv;
  return ESP_OK;
}

static esp_err_t real_init(void) {
  esp_err_t err = ESP_OK;
  if (!s_i2c_initialized) {
    const i2c_master_bus_config_t cfg = {
        .i2c_port = CONFIG_DH_ANALOG_I2C_PORT,
        .sda_io_num = CONFIG_DH_ANALOG_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_DH_ANALOG_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
        .flags.allow_pd = 0,
    };

    err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
      return err;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_DH_ANALOG_ADS1115_ADDR,
        .scl_speed_hz = CONFIG_DH_ANALOG_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags.disable_ack_check = 0,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_ads1115);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
      goto init_fail;
    }

    err = i2c_master_probe(s_i2c_bus, CONFIG_DH_ANALOG_ADS1115_ADDR, 50);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "ADS1115 probe failed: %s", esp_err_to_name(err));
      goto init_fail;
    }

    s_i2c_initialized = true;
  }

  analog_reset_temp_smoothing();

  ESP_LOGI(TAG, "ADS1115 init i2c_port=%d sda=%d scl=%d hz=%d addr=0x%02X", CONFIG_DH_ANALOG_I2C_PORT,
           CONFIG_DH_ANALOG_I2C_SDA_GPIO, CONFIG_DH_ANALOG_I2C_SCL_GPIO, CONFIG_DH_ANALOG_I2C_FREQ_HZ,
           CONFIG_DH_ANALOG_ADS1115_ADDR);

  return ESP_OK;

init_fail:
  if (s_ads1115 != NULL) {
    i2c_master_bus_rm_device(s_ads1115);
    s_ads1115 = NULL;
  }
  if (s_i2c_bus != NULL) {
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
  }
  s_i2c_initialized = false;
  return err;
}

static esp_err_t real_read(analog_sensor_reading_t* out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  int16_t raw_temp = 0;
  int16_t raw_pressure = 0;

  ESP_RETURN_ON_ERROR(ads1115_read_single_ended_raw(ADS1115_MUX_AIN0_GND, &raw_temp), TAG, "temp read failed");
  ESP_RETURN_ON_ERROR(ads1115_read_single_ended_raw(ADS1115_MUX_AIN1_GND, &raw_pressure), TAG, "pressure read failed");

  const float temp_voltage = ((float)raw_temp) * (k_v_fsr / 32767.0f);
  const float pressure_voltage = ((float)raw_pressure) * (k_v_fsr / 32767.0f);

  const float resistance = analog_calculate_resistance_ohms(temp_voltage, k_v_sup, k_bias_ohms);
  const float unsmoothed_temp_f = analog_interpolate_temperature_f(resistance);
  const float smoothed_temp_f = analog_apply_temp_smoothing(unsmoothed_temp_f);
  const float pressure_psi = analog_interpolate_pressure_psi(pressure_voltage);

  out->oil_temp_f = smoothed_temp_f;
  out->oil_pressure_psi = (float)analog_round_to_i16(pressure_psi);
  return ESP_OK;
}

static void real_deinit(void) {
  if (!s_i2c_initialized) {
    return;
  }
  if (s_ads1115 != NULL) {
    i2c_master_bus_rm_device(s_ads1115);
    s_ads1115 = NULL;
  }
  if (s_i2c_bus != NULL) {
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
  }
  s_i2c_initialized = false;
}

static const analog_sensor_backend_t real_backend = {
    .init = real_init,
    .read = real_read,
    .deinit = real_deinit,
};

const analog_sensor_backend_t* analog_sensors_real_backend(void) { return &real_backend; }
