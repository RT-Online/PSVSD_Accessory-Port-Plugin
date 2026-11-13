/* PSVSD/AM persistent configuration. GPL-3.0-or-later. */
#ifndef PSVSD_AM_SETTINGS_H
#define PSVSD_AM_SETTINGS_H
#include <stddef.h>
enum {
#define AM_MOUNT(symbol, value, stem, mount_id) AM_##symbol = value,
#include "mounts.def"
#undef AM_MOUNT
    AM_TARGET_COUNT = 0
#define AM_MOUNT(symbol, value, stem, mount_id) + 1
#include "mounts.def"
#undef AM_MOUNT
};
typedef struct { int enabled, target; } AmSettings;
int am_settings_parse(const char *text, AmSettings *out);
int am_settings_format(const AmSettings *settings, char *out, size_t capacity);
const char *am_target_name(int target);
int am_target_from_name(const char *name, size_t length);
int am_target_mount_id(int target);
#endif
