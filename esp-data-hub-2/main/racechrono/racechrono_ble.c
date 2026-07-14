#include "racechrono_ble.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "sdkconfig.h"
#include "racechrono_packet.h"

static const char* TAG = "racechrono_ble";

#define RACECHRONO_SERVICE_UUID 0x1FF8U
#define RACECHRONO_CAN_MAIN_UUID 0x0001U
#define RACECHRONO_CAN_FILTER_UUID 0x0002U
#define RACECHRONO_CAN_MAX_PAYLOAD 16U
#define RACECHRONO_CAN_PACKET_HEADER_SIZE 4U

static const ble_uuid16_t racechrono_service_uuid = BLE_UUID16_INIT(RACECHRONO_SERVICE_UUID);
static const ble_uuid16_t racechrono_can_main_uuid = BLE_UUID16_INIT(RACECHRONO_CAN_MAIN_UUID);
static const ble_uuid16_t racechrono_can_filter_uuid = BLE_UUID16_INIT(RACECHRONO_CAN_FILTER_UUID);

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t can_main_value_handle;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static bool notifications_enabled;
static bool allow_all_packets;
static bool vehicle_controls_packet_allowed;
static uint16_t notification_interval_ms;
static int64_t last_notification_ms;
static uint8_t last_packet[RACECHRONO_CAN_PACKET_HEADER_SIZE + RACECHRONO_CAN_MAX_PAYLOAD];
static size_t last_packet_len;
static uint8_t own_address_type;

static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event* event, void* arg);

static uint16_t read_be16(const uint8_t* data) {
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t* data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
         (uint32_t)data[3];
}

static int can_main_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt,
                           void* arg) {
  (void)conn_handle;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR || attr_handle != can_main_value_handle) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  uint8_t packet[sizeof(last_packet)];
  size_t packet_len = 0;
  taskENTER_CRITICAL(&state_lock);
  packet_len = last_packet_len;
  memcpy(packet, last_packet, packet_len);
  taskEXIT_CRITICAL(&state_lock);

  if (packet_len == 0) {
    return 0;
  }
  return os_mbuf_append(ctxt->om, packet, packet_len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int can_filter_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt,
                             void* arg) {
  (void)conn_handle;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || attr_handle == 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (OS_MBUF_PKTLEN(ctxt->om) < 1U || OS_MBUF_PKTLEN(ctxt->om) > 7U) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  uint8_t command[7] = {0};
  const uint16_t command_len = OS_MBUF_PKTLEN(ctxt->om);
  if (os_mbuf_copydata(ctxt->om, 0, command_len, command) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  const uint8_t command_id = command[0];
  uint16_t interval_ms = 0;
  uint32_t packet_id = 0;
  if (command_id == 0U) {
    if (command_len != 1U) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
  } else if (command_id == 1U) {
    if (command_len != 3U) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    interval_ms = read_be16(&command[1]);
  } else if (command_id == 2U) {
    if (command_len != 7U) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    interval_ms = read_be16(&command[1]);
    packet_id = read_be32(&command[3]);
  } else {
    return BLE_ATT_ERR_UNLIKELY;
  }

  taskENTER_CRITICAL(&state_lock);
  if (command_id == 0U) {
    allow_all_packets = false;
    vehicle_controls_packet_allowed = false;
    notification_interval_ms = 0;
    last_notification_ms = 0;
  } else if (command_id == 1U) {
    allow_all_packets = true;
    notification_interval_ms = interval_ms;
    last_notification_ms = 0;
  } else if (packet_id == RACECHRONO_PACKET_ID_VEHICLE_CONTROLS) {
    vehicle_controls_packet_allowed = true;
    notification_interval_ms = interval_ms;
    last_notification_ms = 0;
  }
  taskEXIT_CRITICAL(&state_lock);

  ESP_LOGI(TAG, "RaceChrono filter command=%u interval=%u packet=0x%08" PRIX32,
           (unsigned)command_id, (unsigned)interval_ms, packet_id);
  return 0;
}

static const struct ble_gatt_svc_def racechrono_services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &racechrono_service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &racechrono_can_main_uuid.u,
              .access_cb = can_main_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &can_main_value_handle},
             {.uuid = &racechrono_can_filter_uuid.u,
              .access_cb = can_filter_access,
              .flags = BLE_GATT_CHR_F_WRITE},
             {0}}},
    {0},
};

static void start_advertising(void) {
  struct ble_hs_adv_fields fields = {0};
  struct ble_gap_adv_params params = {0};
  const char* name = ble_svc_gap_device_name();

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (const uint8_t*)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;
  fields.uuids16 = (ble_uuid16_t*)&racechrono_service_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set advertising fields: %d", rc);
    return;
  }

  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &params, gap_event_handler, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to start advertising: %d", rc);
  } else {
    ESP_LOGI(TAG, "advertising RaceChrono DIY BLE service");
  }
}

static int gap_event_handler(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        taskENTER_CRITICAL(&state_lock);
        connection_handle = event->connect.conn_handle;
        notifications_enabled = false;
        allow_all_packets = false;
        vehicle_controls_packet_allowed = false;
        notification_interval_ms = 0;
        last_notification_ms = 0;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "RaceChrono connected");
      } else {
        ESP_LOGW(TAG, "BLE connection failed: %d", event->connect.status);
        start_advertising();
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      taskENTER_CRITICAL(&state_lock);
      connection_handle = BLE_HS_CONN_HANDLE_NONE;
      notifications_enabled = false;
      allow_all_packets = false;
      vehicle_controls_packet_allowed = false;
      notification_interval_ms = 0;
      last_notification_ms = 0;
      taskEXIT_CRITICAL(&state_lock);
      ESP_LOGI(TAG, "BLE disconnected: %d", event->disconnect.reason);
      start_advertising();
      return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == can_main_value_handle) {
        taskENTER_CRITICAL(&state_lock);
        connection_handle = event->subscribe.conn_handle;
        notifications_enabled = event->subscribe.cur_notify;
        taskEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "RaceChrono notifications %s", event->subscribe.cur_notify ? "enabled" : "disabled");
      }
      return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      start_advertising();
      return 0;

    default:
      return 0;
  }
}

static void on_stack_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static void on_stack_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "no usable BLE address: %d", rc);
    return;
  }
  rc = ble_hs_id_infer_auto(0, &own_address_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to infer BLE address type: %d", rc);
    return;
  }
  start_advertising();
}

static void nimble_host_task(void* arg) {
  (void)arg;
  nimble_port_run();
  vTaskDelete(NULL);
}

bool racechrono_ble_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_STATE) {
    ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
    return false;
  }

  err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE initialization failed: %s", esp_err_to_name(err));
    return false;
  }

  ble_svc_gap_init();
  int rc = ble_svc_gap_device_name_set(CONFIG_DH_RACECHRONO_BLE_DEVICE_NAME);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set device name: %d", rc);
    return false;
  }
  ble_svc_gatt_init();
  rc = ble_gatts_count_cfg(racechrono_services);
  if (rc == 0) {
    rc = ble_gatts_add_svcs(racechrono_services);
  }
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to register RaceChrono GATT service: %d", rc);
    return false;
  }

  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;

  if (xTaskCreate(nimble_host_task, "nimble_host", 4096, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to create NimBLE host task");
    return false;
  }
  return true;
}

bool racechrono_ble_notify_packet(uint32_t packet_id, const uint8_t* payload, size_t payload_len) {
  if (payload == NULL || payload_len == 0 || payload_len > RACECHRONO_CAN_MAX_PAYLOAD) {
    return false;
  }

  uint8_t packet[RACECHRONO_CAN_PACKET_HEADER_SIZE + RACECHRONO_CAN_MAX_PAYLOAD] = {0};
  packet[0] = (uint8_t)(packet_id & 0xFFU);
  packet[1] = (uint8_t)((packet_id >> 8) & 0xFFU);
  packet[2] = (uint8_t)((packet_id >> 16) & 0xFFU);
  packet[3] = (uint8_t)((packet_id >> 24) & 0xFFU);
  memcpy(&packet[RACECHRONO_CAN_PACKET_HEADER_SIZE], payload, payload_len);
  const size_t packet_len = RACECHRONO_CAN_PACKET_HEADER_SIZE + payload_len;
  const int64_t now_ms = esp_timer_get_time() / 1000;

  uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
  bool should_notify = false;
  taskENTER_CRITICAL(&state_lock);
  const bool packet_enabled = allow_all_packets ||
                              (packet_id == RACECHRONO_PACKET_ID_VEHICLE_CONTROLS && vehicle_controls_packet_allowed);
  const bool interval_elapsed = last_notification_ms == 0 || notification_interval_ms == 0 ||
                                now_ms - last_notification_ms >= notification_interval_ms;
  if (connection_handle != BLE_HS_CONN_HANDLE_NONE && notifications_enabled && packet_enabled && interval_elapsed) {
    conn_handle = connection_handle;
    memcpy(last_packet, packet, packet_len);
    last_packet_len = packet_len;
    should_notify = true;
  }
  taskEXIT_CRITICAL(&state_lock);

  if (!should_notify) {
    return false;
  }

  struct os_mbuf* om = ble_hs_mbuf_from_flat(packet, packet_len);
  if (om == NULL) {
    ESP_LOGW(TAG, "failed to allocate BLE notification buffer");
    return false;
  }
  const int rc = ble_gatts_notify_custom(conn_handle, can_main_value_handle, om);
  if (rc != 0) {
    ESP_LOGW(TAG, "RaceChrono notification failed: %d", rc);
    return false;
  }

  taskENTER_CRITICAL(&state_lock);
  last_notification_ms = now_ms;
  taskEXIT_CRITICAL(&state_lock);
  return true;
}
