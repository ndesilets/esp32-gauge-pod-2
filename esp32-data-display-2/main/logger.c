#include "logger.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "logger";

#define LOG_SAMPLE_PERIOD_US (62500)
#define LOG_TASK_STACK_SIZE (4096)
#define LOG_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define LOG_FILE_NAME_LEN (32)

static bool s_sd_ready = false;
static volatile bool s_logging_active = false;
static FILE* s_log_fp = NULL;
static TaskHandle_t s_log_task = NULL;
static monitored_state_t* s_state_ptr = NULL;
static SemaphoreHandle_t s_state_mutex = NULL;
static SemaphoreHandle_t s_lock = NULL;
static uint64_t s_session_start_us = 0;
static char s_current_path[LOG_FILE_NAME_LEN] = {0};

static void logger_lock(void) {
  if (s_lock) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
  }
}

static void logger_unlock(void) {
  if (s_lock) {
    xSemaphoreGive(s_lock);
  }
}

static esp_err_t find_next_log_index(uint32_t* out_index) {
  DIR* dir = opendir(BSP_SD_MOUNT_POINT);
  if (!dir) {
    return ESP_FAIL;
  }

  uint32_t max_index = 0;
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    const char* name = entry->d_name;
    if (strlen(name) != 13) {
      continue;
    }

    unsigned int parsed_index = 0;
    char trailing = 0;
    if (sscanf(name, "log%6u.csv%c", &parsed_index, &trailing) == 1) {
      if (parsed_index > max_index) {
        max_index = parsed_index;
      }
    }
  }

  closedir(dir);
  *out_index = max_index + 1;
  return ESP_OK;
}

static esp_err_t open_log_file_with_header(void) {
  uint32_t next_index = 0;
  ESP_RETURN_ON_ERROR(find_next_log_index(&next_index), TAG, "Could not find next log index");

  snprintf(s_current_path, sizeof(s_current_path), "%s/log%06u.csv", BSP_SD_MOUNT_POINT, (unsigned int)next_index);
  s_log_fp = fopen(s_current_path, "w");
  if (!s_log_fp) {
    ESP_LOGE(TAG, "Failed opening log file: %s", s_current_path);
    return ESP_FAIL;
  }

  setvbuf(s_log_fp, NULL, _IOLBF, 0);

  if (fprintf(s_log_fp,
              "timestamp_s,water_temp,oil_temp,oil_pressure,dam,af_learned,af_ratio,int_temp,fb_knock,af_correct,"
              "inj_duty,eth_conc\n") < 0) {
    fclose(s_log_fp);
    s_log_fp = NULL;
    ESP_LOGE(TAG, "Failed writing CSV header");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Logging to %s", s_current_path);
  return ESP_OK;
}

static bool write_snapshot_row(FILE* fp, const monitored_state_t* snapshot) {
  double timestamp_s = (double)(esp_timer_get_time() - s_session_start_us) / 1000000.0;
  int rc = fprintf(fp, "%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", timestamp_s,
                   snapshot->water_temp.current_value, snapshot->oil_temp.current_value,
                   snapshot->oil_pressure.current_value, snapshot->dam.current_value, snapshot->af_learned.current_value,
                   snapshot->af_ratio.current_value, snapshot->int_temp.current_value, snapshot->fb_knock.current_value,
                   snapshot->af_correct.current_value, snapshot->inj_duty.current_value, snapshot->eth_conc.current_value);
  return (rc >= 0);
}

static void logger_task(void* arg) {
  FILE* fp = s_log_fp;
  uint64_t next_sample_us = esp_timer_get_time();

  while (s_logging_active) {
    uint64_t now_us = esp_timer_get_time();
    if (now_us < next_sample_us) {
      uint64_t wait_us = next_sample_us - now_us;
      TickType_t wait_ticks = pdMS_TO_TICKS((wait_us + 999) / 1000);
      vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);
      continue;
    }

    monitored_state_t snapshot = {0};
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(10))) {
      snapshot = *s_state_ptr;
      xSemaphoreGive(s_state_mutex);
    } else {
      next_sample_us += LOG_SAMPLE_PERIOD_US;
      continue;
    }

    if (!write_snapshot_row(fp, &snapshot)) {
      ESP_LOGE(TAG, "Write failure, stopping logging");
      s_logging_active = false;
      break;
    }

    next_sample_us += LOG_SAMPLE_PERIOD_US;
  }

  logger_lock();
  if (s_log_fp) {
    fflush(s_log_fp);
    fclose(s_log_fp);
    s_log_fp = NULL;
  }
  s_log_task = NULL;
  s_logging_active = false;
  logger_unlock();

  vTaskDelete(NULL);
}

esp_err_t dd_logger_init(monitored_state_t* state, SemaphoreHandle_t state_mutex) {
  if (!state || !state_mutex) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_lock) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
      return ESP_ERR_NO_MEM;
    }
  }

  logger_lock();
  s_state_ptr = state;
  s_state_mutex = state_mutex;
  logger_unlock();

  esp_err_t err = bsp_sdcard_mount();
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    s_sd_ready = true;
    return ESP_OK;
  }

  s_sd_ready = false;
  return err;
}

esp_err_t dd_logger_start(void) {
  logger_lock();

  if (!s_sd_ready || !s_state_ptr || !s_state_mutex) {
    logger_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  if (s_logging_active || s_log_task != NULL) {
    logger_unlock();
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t open_err = open_log_file_with_header();
  if (open_err != ESP_OK) {
    logger_unlock();
    return open_err;
  }

  s_session_start_us = esp_timer_get_time();
  s_logging_active = true;

  BaseType_t created = xTaskCreate(logger_task, "log_task", LOG_TASK_STACK_SIZE, NULL, LOG_TASK_PRIORITY, &s_log_task);
  if (created != pdPASS) {
    s_logging_active = false;
    fclose(s_log_fp);
    s_log_fp = NULL;
    s_log_task = NULL;
    logger_unlock();
    return ESP_FAIL;
  }

  logger_unlock();
  return ESP_OK;
}

void dd_logger_stop(void) {
  s_logging_active = false;

  for (int i = 0; i < 50; i++) {
    logger_lock();
    bool running = (s_log_task != NULL);
    logger_unlock();
    if (!running) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool dd_logger_is_active(void) { return s_logging_active; }

bool dd_logger_is_ready(void) { return s_sd_ready; }
