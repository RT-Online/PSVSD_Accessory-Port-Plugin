/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMP_CONFIG_H
#define AMP_CONFIG_H

#include <stddef.h>

#define AMP_SETTINGS_PLUGIN "ur0:tai/psvsd_settings.suprx"

typedef struct {
    unsigned has_settings : 1;
    unsigned has_boot : 1;
} AmpConfig;

/* Read taiHEN entries and edit only the paths managed by this installer. */
int amp_config(const char *input, size_t length, char *output, size_t capacity,
               int install, int remove_boot, AmpConfig *current);

#endif
