/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AMP_INSTALL_H
#define AMP_INSTALL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a user-facing result and an error code suitable for the system log. */
int amp_change_installation(int install, char *message, size_t capacity);
int amp_finish_pending_installation(char *message, size_t capacity);
int amp_prepare_reboot(char *message, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
