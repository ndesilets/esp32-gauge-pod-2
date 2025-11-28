#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/// Register LittleFS as drive 'L' in LVGL.
/// Call this after `lv_init()` and after `LittleFS.begin(...)`.
void lv_port_fs_init_littlefs();
#endif