/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "install.h"
#include "config.h"
#include "../src/common/paths.h"
#include "../src/common/settings.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/vshbridge.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOT_FOUND ((int)0x80010002)
#define ALREADY_EXISTS ((int)0x80010011)
#define NO_DEVICE ((int)0x80010013)
#define IO_ERROR ((int)0x80010005)
#define CONFIG_LIMIT 65536
#define FILE_LIMIT (1024 * 1024)
#define WORK_DIR ACCESSORY_KERNEL_DIR "/installer"
#define PENDING WORK_DIR "/pending"
#define COMMITTED WORK_DIR "/committed"
#define CONFIG_BACKUP AM_TAI_CONFIG ".accessory-mount.bak"
#define INSTALLER_PAYLOAD_DIR "app0:/plugins/"
#define TRANSACTION_MAGIC 0x414d5031u

enum {
    CONFIG, SETTINGS_PLUGIN, BOOT_PLUGIN, BOOT_PAYLOAD, STATUS_PAYLOAD, PROBE_PAYLOAD,
    MOUNT_SETTINGS, FILE_COUNT
};

static const struct {
    const char *path;
    const char *payload;
} files[FILE_COUNT] = {
    [CONFIG] = {AM_TAI_CONFIG, NULL},
    [SETTINGS_PLUGIN] = {AMP_SETTINGS_PLUGIN, INSTALLER_PAYLOAD_DIR "psvsd_settings.suprx"},
    [BOOT_PLUGIN] = {AM_BOOT_PLUGIN, INSTALLER_PAYLOAD_DIR "psvsd_am.skprx"},
    [BOOT_PAYLOAD] = {AM_NATIVE_SETTINGS_DIR "/psvsd_am.skprx", INSTALLER_PAYLOAD_DIR "psvsd_am.skprx"},
    [STATUS_PAYLOAD] = {AM_NATIVE_SETTINGS_DIR "/psvsd_am_status.skprx", INSTALLER_PAYLOAD_DIR "psvsd_am_status.skprx"},
    [PROBE_PAYLOAD] = {AM_NATIVE_SETTINGS_DIR "/usbmc_accessory_probe.skprx", INSTALLER_PAYLOAD_DIR "usbmc_accessory_probe.skprx"},
    [MOUNT_SETTINGS] = {AM_SETTINGS_PATH, NULL},
};

typedef struct {
    uint32_t magic, changed, existed, writes, install;
} Transaction;

static unsigned bit(unsigned i) { return 1u << i; }

static int file_status(const char *path, size_t *size) {
    SceIoStat stat;
    int ret = sceIoGetstat(path, &stat);
    if (ret < 0) return ret;
    if (!SCE_S_ISREG(stat.st_mode) || (uint64_t)stat.st_size > FILE_LIMIT) return IO_ERROR;
    *size = (size_t)stat.st_size;
    return 0;
}

static int read_file(const char *path, size_t limit, char **data, size_t *size) {
    *data = NULL;
    int ret = file_status(path, size);
    if (ret < 0) return ret;
    if (*size > limit) return IO_ERROR;
    char *bytes = malloc(*size + 1);
    if (!bytes) return IO_ERROR;
    int fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        free(bytes);
        return fd;
    }
    size_t used = 0;
    while (used < *size) {
        ret = sceIoRead(fd, bytes + used, *size - used);
        if (ret <= 0) break;
        used += (size_t)ret;
    }
    char extra;
    if (used == *size) ret = sceIoRead(fd, &extra, 1);
    int closed = sceIoClose(fd);
    if (ret || closed < 0 || used != *size) {
        free(bytes);
        return ret < 0 ? ret : closed < 0 ? closed : IO_ERROR;
    }
    bytes[*size] = '\0';
    *data = bytes;
    return 0;
}

static int remove_file(const char *path) {
    SceIoStat stat;
    int ret = sceIoGetstat(path, &stat);
    if (ret >= 0) ret = SCE_S_ISREG(stat.st_mode) ? sceIoRemove(path) : IO_ERROR;
    return ret == NOT_FOUND ? 0 : ret;
}

static int sync_and_close(int fd) {
    int ret = sceIoSyncByFd(fd, 0);
    int closed = sceIoClose(fd);
    return ret < 0 ? ret : closed;
}

static int write_file(const char *path, const void *data, size_t size) {
    int fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0666);
    if (fd < 0) return fd;
    const char *bytes = data;
    int ret = 0;
    while (size) {
        int n = sceIoWrite(fd, bytes, size);
        if (n <= 0) {
            ret = n < 0 ? n : IO_ERROR;
            break;
        }
        bytes += n;
        size -= (size_t)n;
    }
    if (ret >= 0) ret = sync_and_close(fd);
    else sceIoClose(fd);
    if (ret < 0) remove_file(path);
    return ret;
}

static void temporary_path(char out[128], unsigned i, const char *suffix) {
    snprintf(out, 128, "%s.amp-%s", files[i].path, suffix);
}

static int sync_file(const char *path) {
    int fd = sceIoOpen(path, SCE_O_RDWR, 0);
    return fd < 0 ? fd : sync_and_close(fd);
}

static int flush(const char *journal) {
    int ret = sceIoSync("ur0:", 0);
    if (ret != NO_DEVICE) return ret;
    ret = sceIoSync("ur0:/", 0);
    if (ret != NO_DEVICE) return ret;

    /* Keep file-level sync when the mount-name API returns ENODEV. */
    ret = sync_file(AM_TAI_CONFIG);
    if (ret >= 0 && journal) ret = sync_file(journal);
    return ret;
}

static int clean_files(int discard_previous, const char **step) {
    *step = discard_previous ? "clear completed setup files" : "check the previous setup files";
    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        char path[128];
        temporary_path(path, i, "old");
        if (discard_previous) {
            int ret = remove_file(path);
            if (ret < 0) return ret;
        } else {
            size_t size;
            int ret = file_status(path, &size);
            if (ret != NOT_FOUND) return ret < 0 ? ret : IO_ERROR;
        }
    }
    *step = "clear staged setup files";
    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        char path[128];
        temporary_path(path, i, "new");
        int ret = remove_file(path);
        if (ret < 0) return ret;
    }
    return remove_file(PENDING ".new");
}

static unsigned file_in_order(int install, unsigned step) {
    /* Register after staging the driver. Unregister before removing it. */
    return install ? (step + 1) % FILE_COUNT : step;
}

static int read_transaction(const char *path, Transaction *tx) {
    size_t size;
    char *bytes;
    int ret = read_file(path, sizeof(*tx), &bytes, &size);
    if (ret < 0) return ret;
    if (size == sizeof(*tx)) memcpy(tx, bytes, sizeof(*tx));
    free(bytes);
    if (size != sizeof(*tx) || tx->magic != TRANSACTION_MAGIC || !tx->changed ||
        tx->changed >= bit(FILE_COUNT) || (tx->existed & ~tx->changed) ||
        (tx->writes & ~tx->changed) || tx->install > 1) return IO_ERROR;
    return 0;
}

static int finish_transaction(const char *journal, const char **step) {
    *step = "sync the recovered files";
    int ret = flush(journal);
    if (ret < 0) return ret;
    *step = "clear the installer journal";
    ret = remove_file(journal);
    if (ret < 0) return ret;
    *step = "sync the cleared installer journal";
    return flush(NULL);
}

static int recover_transaction(const char **step) {
    Transaction tx;
    *step = "read the completed setup journal";
    int ret = read_transaction(COMMITTED, &tx);
    if (ret != NOT_FOUND) {
        if (ret < 0) return ret;
        *step = "check for a second setup journal";
        size_t size;
        ret = file_status(PENDING, &size);
        if (ret != NOT_FOUND) return ret < 0 ? ret : IO_ERROR;
        *step = "sync the completed setup";
        ret = flush(COMMITTED);
        if (ret < 0) return ret;
        ret = clean_files(1, step);
        return ret < 0 ? ret : finish_transaction(COMMITTED, step);
    }
    *step = "read the pending setup journal";
    ret = read_transaction(PENDING, &tx);
    if (ret == NOT_FOUND) return clean_files(0, step);
    if (ret < 0) return ret;

    *step = "restore the previous setup files";
    for (unsigned index = FILE_COUNT; index > 0; --index) {
        unsigned i = file_in_order(tx.install, index - 1);
        if (!(tx.changed & bit(i))) continue;
        char old[128];
        temporary_path(old, i, "old");
        size_t size;
        ret = file_status(old, &size);
        if (ret == NOT_FOUND) {
            ret = tx.existed & bit(i) ? file_status(files[i].path, &size) : remove_file(files[i].path);
        } else if (ret >= 0) {
            ret = remove_file(files[i].path);
            if (ret >= 0) ret = sceIoRename(old, files[i].path);
        }
        if (ret < 0) return ret;
    }
    ret = clean_files(0, step);
    return ret < 0 ? ret : finish_transaction(PENDING, step);
}

int amp_finish_pending_installation(char *message, size_t capacity) {
    const char *step;
    SceIoStat stat;
    int ret = sceIoGetstat("ur0:tai", &stat);
    if (ret >= 0 && !SCE_S_ISDIR(stat.st_mode)) ret = IO_ERROR;
    if (ret < 0) {
        snprintf(message, capacity, "Cannot access ur0:tai (%08X).\nCheck the folder in VitaShell.", (unsigned)ret);
        return ret;
    }
    ret = recover_transaction(&step);
    if (ret < 0)
        snprintf(message, capacity, "Could not %s (%08X).\nSee the README before rebooting.", step, (unsigned)ret);
    return ret;
}

int amp_prepare_reboot(char *message, size_t capacity) {
    int ret = amp_finish_pending_installation(message, capacity);
    if (ret < 0) return ret;
    ret = flush(NULL);
    if (ret < 0)
        snprintf(message, capacity, "Could not sync the taiHEN config (%08X).", (unsigned)ret);
    return ret;
}

static int same_file(const char *path, const char *bytes, size_t size) {
    char *current;
    size_t length;
    if (read_file(path, FILE_LIMIT, &current, &length) < 0) return 0;
    int same = length == size && !memcmp(current, bytes, size);
    free(current);
    return same;
}

static int stage(Transaction *tx, unsigned i, const char *bytes, size_t length) {
    size_t size;
    int present = file_status(files[i].path, &size);
    if (present < 0 && present != NOT_FOUND) return present;
    if (!bytes && present == NOT_FOUND) return 0;
    if (bytes && present == 0 && size == length && same_file(files[i].path, bytes, length)) return 0;
    if (bytes) {
        char path[128];
        temporary_path(path, i, "new");
        int ret = write_file(path, bytes, length);
        if (ret < 0) return ret;
        if (!same_file(path, bytes, length)) return IO_ERROR;
        tx->writes |= bit(i);
    }
    tx->changed |= bit(i);
    if (present == 0) tx->existed |= bit(i);
    return 0;
}

static int commit(const Transaction *tx, const char **step) {
    if (!tx->changed) return 0;
    *step = "save the pending setup journal";
    int ret = write_file(PENDING ".new", tx, sizeof(*tx));
    if (ret >= 0) ret = sceIoRename(PENDING ".new", PENDING);
    if (ret < 0) return ret;
    *step = "sync the pending setup journal";
    ret = flush(PENDING);
    if (ret < 0) return ret;
    *step = "replace the setup files";
    for (unsigned index = 0; index < FILE_COUNT; ++index) {
        unsigned i = file_in_order(tx->install, index);
        if (!(tx->changed & bit(i))) continue;
        if (tx->existed & bit(i)) {
            char old[128];
            temporary_path(old, i, "old");
            ret = sceIoRename(files[i].path, old);
        }
        if (ret >= 0 && (tx->writes & bit(i))) {
            char path[128];
            temporary_path(path, i, "new");
            ret = sceIoRename(path, files[i].path);
        }
        if (ret < 0) return ret;
    }
    *step = "sync the installed files";
    ret = flush(PENDING);
    if (ret < 0) return ret;
    *step = "complete the setup journal";
    return sceIoRename(PENDING, COMMITTED);
}

static int save_config_backup(const char *input, size_t length) {
    size_t size;
    int ret = file_status(CONFIG_BACKUP, &size);
    if (ret == NOT_FOUND) {
        ret = write_file(CONFIG_BACKUP, input, length);
        if (ret >= 0 && !same_file(CONFIG_BACKUP, input, length)) {
            remove_file(CONFIG_BACKUP);
            ret = IO_ERROR;
        }
    }
    return ret;
}

static int make_directories(void) {
    const char *directories[] = {"ur0:data", ACCESSORY_KERNEL_DIR, AM_NATIVE_SETTINGS_DIR, WORK_DIR};
    for (size_t i = 0; i < sizeof(directories) / sizeof(*directories); ++i) {
        int ret = sceIoMkdir(directories[i], 0777);
        if (ret < 0 && ret != ALREADY_EXISTS) return ret;
    }
    return 0;
}

static int read_mount_settings(AmSettings *settings) {
    char *text;
    size_t size;
    int ret = read_file(AM_SETTINGS_PATH, 1024, &text, &size);
    if (ret == NOT_FOUND) ret = read_file(AM_LEGACY_SETTINGS_PATH, 1024, &text, &size);
    if (ret < 0) return ret;
    ret = memchr(text, '\0', size) ? IO_ERROR : am_settings_parse(text, settings);
    free(text);
    return ret;
}

static int main_storage_in_use(void) {
    unsigned char scratch[8] = {0};
    int running = _vshKernelSearchModuleByName(AM_KERNEL_NAME, scratch) >= 0;
    AmSettings settings;
    if (read_mount_settings(&settings) < 0) return running;
    if (settings.target == AM_UX0 && (settings.enabled || running)) return 1;
    if (!running) return 0;
    char *report;
    size_t length;
    int ret = read_file(ACCESSORY_KERNEL_DIR "/runtime.txt", 1024, &report, &length);
    if (ret < 0) return 1;
    const char *mount = strstr(report, "\nMount: ");
    int blocked = !mount || !strncmp(mount + 8, "ux0:", 4);
    free(report);
    return blocked;
}

static int existing_boot_driver(void) {
    char *driver;
    size_t length;
    int ret = read_file(AM_BOOT_PLUGIN, FILE_LIMIT, &driver, &length);
    if (ret == NOT_FOUND) return 0;
    if (ret < 0) return ret;
    int owned = same_file(files[BOOT_PAYLOAD].path, driver, length) ||
                same_file(files[BOOT_PLUGIN].payload, driver, length);
    free(driver);
    return owned ? 1 : 2;
}

static int stage_payloads(Transaction *tx, int install, int driver) {
    for (unsigned i = SETTINGS_PLUGIN; i <= PROBE_PAYLOAD; ++i) {
        if (i == BOOT_PLUGIN && !install && driver != 1) continue;
        char *bytes = NULL;
        size_t length = 0;
        int ret = install ? read_file(files[i].payload, FILE_LIMIT, &bytes, &length) : 0;
        if (ret >= 0 && install && (length < 4 || memcmp(bytes, "SCE\0", 4))) ret = IO_ERROR;
        if (ret >= 0) ret = stage(tx, i, bytes, length);
        free(bytes);
        if (ret < 0) return ret;
    }
    return 0;
}

static int change_installation(int install, char *message, size_t capacity) {
    size_t length;
    int ret = file_status("ux0:tai/config.txt", &length);
    if (ret >= 0) {
        snprintf(message, capacity, "taiHEN config also exists in ux0:tai.\nSee the README before continuing.");
        return IO_ERROR;
    }
    if (ret != NOT_FOUND) {
        snprintf(message, capacity, "Could not check ux0:tai/config.txt (%08X).", (unsigned)ret);
        return ret;
    }
    char *input;
    ret = read_file(AM_TAI_CONFIG, CONFIG_LIMIT, &input, &length);
    if (ret < 0) {
        snprintf(message, capacity, "Could not read ur0:tai/config.txt (%08X).", (unsigned)ret);
        return ret;
    }
    char *output = malloc(length + 96);
    if (!output) {
        snprintf(message, capacity, "Could not allocate memory for the taiHEN config.");
        free(input);
        return IO_ERROR;
    }
    Transaction tx = {.magic = TRANSACTION_MAGIC, .install = (unsigned)install};
    const char *step = "check the existing storage driver";
    int driver = existing_boot_driver();
    ret = driver;
    if (driver < 0) goto done;
    if (install && driver == 2) {
        snprintf(message, capacity, "A different PSVSD/AM driver is installed.\nDisable it and follow the migration steps in the README.");
        ret = IO_ERROR;
        goto done;
    }
    AmpConfig current;
    int edited = amp_config(input, length, output, length + 96, install, !install && driver < 2, &current);
    if (edited < 0) {
        snprintf(message, capacity, "Could not edit taiHEN config. Check the file in VitaShell.");
        ret = IO_ERROR;
        goto done;
    }
    if (!install && driver < 2 && (current.has_boot || driver == 1) && main_storage_in_use()) {
        snprintf(message, capacity, "Disable Accessory Port in Settings and reboot first.\nThe driver may be providing main storage.");
        ret = IO_ERROR;
        goto done;
    }
    step = "stage the plug-in files";
    ret = stage_payloads(&tx, install, driver);
    if (ret < 0) goto done;
    if (!install && driver < 2 && (current.has_settings || current.has_boot || driver == 1)) {
        step = "save the accessory-port settings";
        AmSettings settings;
        if (read_mount_settings(&settings) >= 0 && settings.enabled) {
            char disabled[128];
            settings.enabled = 0;
            int size = am_settings_format(&settings, disabled, sizeof(disabled));
            if (size < 0) {
                ret = IO_ERROR;
                goto done;
            }
            ret = stage(&tx, MOUNT_SETTINGS, disabled, (size_t)size);
            if (ret < 0) goto done;
        }
    }
    if (!same_file(AM_TAI_CONFIG, input, length)) {
        snprintf(message, capacity, "taiHEN config changed during setup. Try again.");
        ret = IO_ERROR;
        goto done;
    }
    step = "stage the taiHEN config";
    ret = stage(&tx, CONFIG, output, (size_t)edited);
    if (ret < 0) goto done;
    if (tx.changed & bit(CONFIG)) {
        step = "back up the taiHEN config";
        ret = save_config_backup(input, length);
        if (ret < 0) goto done;
    }
    ret = commit(&tx, &step);
    if (ret >= 0) {
        const char *status = install
            ? "Installed. Reboot, then open Settings > Devices > PSVSD Accessory Port."
            : driver == 2 ? "Removed Native Settings. The separate PSVSD/AM driver is still installed."
                          : "Accessory Mount Plugin uninstalled. Reboot to apply.";
        snprintf(message, capacity, "%s", status);
    }
done:
    if (ret < 0 && !message[0])
        snprintf(message, capacity, "Could not %s (%08X).", step, (unsigned)ret);
    free(output);
    free(input);
    return ret;
}

int amp_change_installation(int install, char *message, size_t capacity) {
    if (!capacity) return IO_ERROR;
    message[0] = '\0';
    int ret = amp_finish_pending_installation(message, capacity);
    if (ret < 0) return ret;
    ret = make_directories();
    if (ret < 0) {
        snprintf(message, capacity, "Could not create the setup folder on ur0: (%08X).", (unsigned)ret);
        return ret;
    }
    ret = change_installation(install, message, capacity);
    int cleanup = amp_finish_pending_installation(message, capacity);
    return cleanup < 0 ? cleanup : ret;
}
