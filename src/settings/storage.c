/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "storage.h"
#include "platform.h"
#include "manager.h"
#include "session.h"
#include <stdio.h>

static AccessorySession probe;
static unsigned char storage_busy;

static int read_status(int prepare_original, AmStatus *status) {
    am_expect_query(0);
    am_status(status);
    if (!status->loaded) return 0;

    uint32_t token = sceKernelGetProcessTimeLow();
    if (!token) token = 1;
    char old[256];
    uint32_t previous = 0;
    if (am_read_text(ACCESSORY_KERNEL_DIR "/runtime.txt", old, sizeof(old)) >= 0 &&
        am_parse_query_header(old, &previous) &&
        previous == token) token = token == UINT32_MAX ? 1 : token + 1;

    AccessoryQuery query = {-1, -1, -1, -1};
    int stage = am_stage_status();
    int ret = stage;
    if (stage >= 0) {
        ret = prepare_original ? accessory_original_prepare(token, &query) :
            accessory_status_query(token, &query);
    }
    am_expect_query(ret >= 0 ? token : 0);
    am_status(status);
    if (ret < 0 || query.unload < 0) {
        am_log("Native Settings storage query: stage=%08X load=%08X start=%08X entry=%08X unload=%08X\n",
               (unsigned)stage, (unsigned)query.load, (unsigned)query.start,
               (unsigned)query.entry, (unsigned)query.unload);
    }
    return ret;
}

static int request_usb(void *context, char *message, size_t capacity) {
    (void)context;
    AmStatus status;
    am_status(&status);
    if (status.legacy_loaded) {
        snprintf(message, capacity, "Remove the older USB plugin and restart. See Log.");
        return -1;
    }
    if (!status.loaded && !status.uma0) {
        accessory_session_run(&probe);
        am_log("Native Settings USB probe: stage=%08X load=%08X start=%08X entry=%08X\n",
               (unsigned)probe.stage_result, (unsigned)probe.load_result,
               (unsigned)probe.start_result, (unsigned)probe.entry_result);
    }
    int ret = read_status(0, &status);
    if (ret < 0 || !status.verified_mount || status.actual_target == AM_UX0) {
        snprintf(message, capacity, "PSVSD secondary storage could not be verified. See Log.");
        return ret < 0 ? ret : -1;
    }
    return 0;
}

static int request_original(void *context, char *message, size_t capacity) {
    (void)context;
    AmStatus status;
    int ret = read_status(1, &status);
    if (ret < 0 || !status.original_ready) {
        snprintf(message, capacity, "Original storage could not be verified at uma0:. See Log.");
        return ret < 0 ? ret : -1;
    }
    return 0;
}

int psvsd_read_option(PsvsdOption option, int *value) {
    if (!value || (option != PSVSD_ENABLED && option != PSVSD_TARGET)) return -1;
    AmSettings saved;
    int ret = am_load_settings(&saved);
    if (ret < 0) return ret;
    *value = option == PSVSD_ENABLED ? saved.enabled : saved.target;
    return 0;
}

static int save_option(PsvsdOption option, int value) {
    AmSettings saved;
    int ret = am_load_settings(&saved);
    if (ret < 0) {
        am_log("Native Settings cannot read saved options: %08X\n", (unsigned)ret);
        return ret;
    }
    int *current = option == PSVSD_ENABLED ? &saved.enabled : &saved.target;
    if (*current == value) return 0;
    *current = value;

    AmStatus status;
    read_status(0, &status);
    char message[192];
    const AmSaveCallbacks callbacks = {
        .request_usb = request_usb,
        .request_original = request_original,
    };
    return am_save_settings_with_callbacks(&saved, &callbacks, message, sizeof(message));
}

int psvsd_save_option(PsvsdOption option, int value) {
    if (!psvsd_option_value_valid(option, value)) return -1;
    if (__atomic_test_and_set(&storage_busy, __ATOMIC_ACQUIRE)) return -1;
    int ret = save_option(option, value);
    __atomic_clear(&storage_busy, __ATOMIC_RELEASE);
    return ret;
}

int psvsd_option_value_valid(PsvsdOption option, int value) {
    if (option == PSVSD_ENABLED) return value == 0 || value == 1;
    return option == PSVSD_TARGET && (unsigned)value < AM_TARGET_COUNT;
}

int psvsd_begin_restart(void) {
    return __atomic_test_and_set(&storage_busy, __ATOMIC_ACQUIRE) ? -1 : 0;
}

void psvsd_cancel_restart(void) {
    __atomic_clear(&storage_busy, __ATOMIC_RELEASE);
}
