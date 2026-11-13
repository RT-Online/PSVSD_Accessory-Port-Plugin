/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSVSD_SETTINGS_LAYOUT_H
#define PSVSD_SETTINGS_LAYOUT_H

#include <stddef.h>

#define PSVSD_CATEGORY "/CONFIG/PSVSDAM"
#define PSVSD_ENABLE_KEY "enabled"
#define PSVSD_TARGET_KEY "target"
#define PSVSD_PAGE_ID "psvsd_accessory_port"
#define PSVSD_LAYOUT_LIMIT (256u * 1024u)

extern const char psvsd_page_xml[];
extern const size_t psvsd_page_xml_size;

typedef struct {
    size_t insertion;
    int yamt_page;
} PsvsdLayout;

/* Finds where to add a row under Devices and whether YAMT has a row there. */
int psvsd_find_devices_layout(const char *xml, size_t length, PsvsdLayout *layout);

#endif
