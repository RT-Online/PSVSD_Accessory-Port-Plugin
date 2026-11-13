/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSVSD_SETTINGS_STORAGE_H
#define PSVSD_SETTINGS_STORAGE_H

typedef enum { PSVSD_ENABLED, PSVSD_TARGET } PsvsdOption;

int psvsd_read_option(PsvsdOption option, int *value);
int psvsd_save_option(PsvsdOption option, int value);
int psvsd_option_value_valid(PsvsdOption option, int value);
int psvsd_begin_restart(void);
void psvsd_cancel_restart(void);

#endif
