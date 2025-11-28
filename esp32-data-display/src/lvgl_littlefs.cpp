#include "lvgl_littlefs.h"

#include <LittleFS.h>

// Driver must be static/global; LVGL stores only a pointer to it
static lv_fs_drv_t littlefs_drv;

static void lv_littlefs_fs_init(void);

void lv_port_fs_init_littlefs() { lv_littlefs_fs_init(); }

static void* littlefs_open(lv_fs_drv_t* drv, const char* path,
                           lv_fs_mode_t mode) {
  LV_UNUSED(drv);

  char full_path[64];
  if (path[0] == '/') {
    snprintf(full_path, sizeof(full_path), "%s", path);
  } else {
    snprintf(full_path, sizeof(full_path), "/%s", path);
  }

  const char* open_mode = nullptr;
  switch (mode) {
    case LV_FS_MODE_WR:
      open_mode = "w";
      break;
    case LV_FS_MODE_RD:
      open_mode = "r";
      break;
    case LV_FS_MODE_WR | LV_FS_MODE_RD:
      open_mode = "r+";
      break;
    default:
      return nullptr;
  }

  File* f = new File(LittleFS.open(full_path, open_mode));
  if (!f || !(*f)) {
    delete f;
    return nullptr;
  }

  return f;
}

static lv_fs_res_t littlefs_close(lv_fs_drv_t* drv, void* file_p) {
  LV_UNUSED(drv);

  File* f = (File*)file_p;

  if (f) {
    f->close();
    delete f;
  }

  return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_read(lv_fs_drv_t* drv, void* file_p, void* buf,
                                 uint32_t bytes_to_read, uint32_t* bytes_read) {
  LV_UNUSED(drv);

  File* f = (File*)file_p;
  if (!f) {
    if (bytes_read) *bytes_read = 0;
    return LV_FS_RES_FS_ERR;
  }

  size_t r = f->read(static_cast<uint8_t*>(buf), bytes_to_read);
  if (bytes_read) *bytes_read = r;

  return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos,
                                 lv_fs_whence_t whence) {
  LV_UNUSED(drv);
  File* f = (File*)file_p;
  if (!f) {
    return LV_FS_RES_FS_ERR;
  }

  uint32_t base = 0;
  if (whence == LV_FS_SEEK_CUR) {
    base = f->position();
  } else if (whence == LV_FS_SEEK_END) {
    base = f->size();
  }

  if (!f->seek(base + pos)) {
    return LV_FS_RES_FS_ERR;
  }

  return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_tell(lv_fs_drv_t* drv, void* file_p,
                                 uint32_t* pos_p) {
  LV_UNUSED(drv);
  File* f = (File*)file_p;

  if (!f || !pos_p) {
    return LV_FS_RES_FS_ERR;
  }

  *pos_p = f->position();
  return LV_FS_RES_OK;
}

static lv_fs_res_t littlefs_size(lv_fs_drv_t* drv, void* file_p,
                                 uint32_t* size_p) {
  LV_UNUSED(drv);
  File* f = (File*)file_p;
  if (!f || !size_p) {
    return LV_FS_RES_FS_ERR;
  }

  *size_p = f->size();
  return LV_FS_RES_OK;
}

static bool littlefs_ready(lv_fs_drv_t* drv) {
  LV_UNUSED(drv);
  return true;
}

static void lv_littlefs_fs_init(void) {
  // Init static driver struct
  lv_fs_drv_init(&littlefs_drv);

  littlefs_drv.letter = 'L';    // "L:/path/to/file"
  littlefs_drv.cache_size = 0;  // no internal caching

  littlefs_drv.ready_cb = littlefs_ready;
  littlefs_drv.open_cb = littlefs_open;
  littlefs_drv.close_cb = littlefs_close;
  littlefs_drv.read_cb = littlefs_read;
  littlefs_drv.write_cb = NULL;  // or NULL if you don't need writes
  littlefs_drv.seek_cb = littlefs_seek;
  littlefs_drv.tell_cb = littlefs_tell;

  // We don’t implement dir_* here; they can stay NULL
  littlefs_drv.dir_open_cb = nullptr;
  littlefs_drv.dir_read_cb = nullptr;
  littlefs_drv.dir_close_cb = nullptr;

  littlefs_drv.user_data = nullptr;

  lv_fs_drv_register(&littlefs_drv);
}
