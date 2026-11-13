/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSVSD_SETTINGS_FOOTER_H
#define PSVSD_SETTINGS_FOOTER_H

#include <stdint.h>

/* The PAF ID passed by Settings carries a string and its resource hash. */
typedef struct {
    const char *text;
    uint32_t length, reserved, hash;
} PsvsdPafId;

typedef struct PsvsdPafTemplateParams PsvsdPafTemplateParams;

int psvsd_footer_init(void);
int psvsd_footer_is_accessory_page(const PsvsdPafId *id);
int psvsd_footer_is_list_template(const PsvsdPafId *id);
int psvsd_footer_add(void *plugin, void *scene, int (*reboot)(void));
int psvsd_footer_in_use(void);

#endif
