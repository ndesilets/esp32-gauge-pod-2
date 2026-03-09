#include "usb_drive.h"

#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "logger.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

static const char* TAG = "usb_drive";

static volatile bool s_host_connected = false;
static sdmmc_card_t* s_card = NULL;

/* ── USB descriptors ───────────────────────────────────────────────── */

#define EPNUM_MSC 1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };

enum {
  EDPT_MSC_OUT = 0x01,
  EDPT_MSC_IN = 0x81,
};

static tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_fs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t s_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0,
};

static const uint8_t s_hs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 512),
};
#endif

static const char* s_string_desc[] = {
    (const char[]){0x09, 0x04},  // supported language: English
    "Gauge Pod",                 // manufacturer
    "Data Logger",               // product
    "000001",                    // serial
    "SD Card",                   // MSC interface
};

/* ── Mount-change callback ─────────────────────────────────────────── */

static void storage_mount_changed_cb(tinyusb_msc_event_t* event) {
  bool mounted_to_app = event->mount_changed_data.is_mounted;
  ESP_LOGI(TAG, "Storage mounted to app: %s", mounted_to_app ? "Yes" : "No");

  if (!mounted_to_app) {
    // PC has taken the drive. Set the flag so the logger won't be restarted.
    // The logger task will stop itself naturally on the next write failure
    // (VFS is already unmounted by the time this callback fires), so we
    // deliberately avoid calling dd_logger_stop() here to keep this callback
    // non-blocking and the TinyUSB task responsive.
    s_host_connected = true;
    if (dd_logger_is_active()) {
      ESP_LOGW(TAG, "USB host connected; logger will stop on next write");
    }
  } else {
    s_host_connected = false;
  }
}

/* ── SD card init (mirrors BSP slot 0 config) ─────────────────────── */

static esp_err_t init_sdmmc_card(sdmmc_card_t** out_card) {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  // LDO channel 4 is already powered by the display BSP (bsp_enable_ldo_vo4).
  // Do not re-acquire it here — esp_ldo_acquire_channel is exclusive.

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;

  sdmmc_card_t* card = malloc(sizeof(sdmmc_card_t));
  ESP_RETURN_ON_FALSE(card, ESP_ERR_NO_MEM, TAG, "Card alloc failed");

  ESP_RETURN_ON_ERROR((*host.init)(), TAG, "SDMMC host init failed");
  ESP_RETURN_ON_ERROR(sdmmc_host_init_slot(host.slot, &slot_config), TAG, "SDMMC slot init failed");
  ESP_RETURN_ON_ERROR(sdmmc_card_init(&host, card), TAG, "SD card init failed");

  sdmmc_card_print_info(stdout, card);
  *out_card = card;
  return ESP_OK;
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t dd_usb_drive_init(void) {
  ESP_RETURN_ON_ERROR(init_sdmmc_card(&s_card), TAG, "SD card init failed");

  // Set BSP global for compatibility
  bsp_sdcard = s_card;

  const tinyusb_msc_sdmmc_config_t msc_cfg = {
      .card = s_card,
      .callback_mount_changed = storage_mount_changed_cb,
      .mount_config.max_files = 5,
  };
  ESP_RETURN_ON_ERROR(tinyusb_msc_storage_init_sdmmc(&msc_cfg), TAG, "MSC storage init failed");
  ESP_RETURN_ON_ERROR(tinyusb_msc_storage_mount(BSP_SD_MOUNT_POINT), TAG, "MSC storage mount failed");

  const tinyusb_config_t tusb_cfg = {
      .device_descriptor = &s_device_desc,
      .string_descriptor = s_string_desc,
      .string_descriptor_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]),
      .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
      .fs_configuration_descriptor = s_fs_config_desc,
      .hs_configuration_descriptor = s_hs_config_desc,
      .qualifier_descriptor = &s_device_qualifier,
#else
      .configuration_descriptor = s_fs_config_desc,
#endif
  };
  ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "TinyUSB driver install failed");

  ESP_LOGI(TAG, "USB MSC initialized, SD card at %s", BSP_SD_MOUNT_POINT);
  return ESP_OK;
}

bool dd_usb_drive_is_host_connected(void) { return s_host_connected; }
