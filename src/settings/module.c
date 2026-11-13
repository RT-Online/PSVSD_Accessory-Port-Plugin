/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "layout.h"
#include "storage.h"
#include "manager.h"
#include "footer.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <taihen.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_MODULE "SceSettings"
#define SETTINGS_CORE "SceSystemSettingsCore"
#define SETTINGS_CORE_FILE "system_settings_core.suprx"
#define LIB_KERNEL 0xCAE9ACE6u
#define LIB_REGISTRY 0xC436F916u
#define LIB_PAF_MISC 0x3D643CE8u
#define LIB_PAF_TOPLEVEL 0x4D9A9DD0u
#define DEVICES_PLUGIN "peripherals_settings_plugin"
#define YAMT_PLUGIN_NAME_OFFSET 85u
#define SETTINGS_LOG AM_PAYLOAD_ROOT "settings.log"

typedef struct {
    SceUID id;
    tai_hook_ref_t ref;
} Hook;

static Hook load_core = {-1, 0}, unload_core = {-1, 0};
static Hook get_value = {-1, 0}, set_value = {-1, 0}, get_info = {-1, 0}, load_layout = {-1, 0};
static Hook open_page = {-1, 0}, open_template = {-1, 0};
static SceUID core_id = -1;
static int page_active;
static void *accessory_scene;

static void trace(const char *format, ...) {
    int fd = sceIoOpen(SETTINGS_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) return;
    char line[192];
    va_list args;
    va_start(args, format);
    int n = sceClibVsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n > 0) sceIoWrite(fd, line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    sceIoClose(fd);
}

static int add_hook(Hook *hook, const char *module, uint32_t library, uint32_t function, const void *callback) {
    if (hook->id >= 0) return 0;
    hook->id = taiHookFunctionImport(&hook->ref, module, library, function, callback);
    if (hook->id < 0) {
        trace("Hook %s %08X failed: %08X\n", module, (unsigned)function, (unsigned)hook->id);
    }
    return hook->id;
}

static int remove_hook(Hook *hook) {
    if (hook->id < 0) return 0;
    int ret = taiHookRelease(hook->id, hook->ref);
    if (ret < 0) {
        trace("Hook release failed: %08X\n", (unsigned)ret);
        return ret;
    }
    hook->id = -1;
    return 0;
}

static int our_category(const char *category) {
    return page_active && category && !strcmp(category, PSVSD_CATEGORY);
}

static int request_reboot(void) {
    if (!page_active) return -1;
    if (psvsd_begin_restart() < 0) {
        trace("Reboot Device: storage is busy\n");
        return -1;
    }
    trace("Reboot Device pressed\n");
    int ret = scePowerRequestColdReset();
    trace("Cold reset request: %08X\n", (unsigned)ret);
    if (ret < 0) {
        psvsd_cancel_restart();
    }
    return ret;
}

static void *settings_page_open(void *plugin, const PsvsdPafId *id, const void *parameters) {
    void *scene = TAI_CONTINUE(void *, open_page.ref, plugin, id, parameters);
    if (page_active) accessory_scene = psvsd_footer_is_accessory_page(id) ? scene : NULL;
    return scene;
}

static int settings_template_open(void *plugin, void *parent, const PsvsdPafId *id,
                                  const PsvsdPafTemplateParams *parameters) {
    int ret = TAI_CONTINUE(int, open_template.ref, plugin, parent, id, parameters);
    /* The list template is added after the page opens. Append the button above its cells. */
    if (page_active && parent && parent == accessory_scene && psvsd_footer_is_list_template(id)) {
        accessory_scene = NULL;
        if (ret >= 0) {
            int added = psvsd_footer_add(plugin, parent, request_reboot);
            trace("Reboot Device footer: %08X\n", (unsigned)added);
        }
    }
    return ret;
}

static int option_for_key(const char *category, const char *key, PsvsdOption *option) {
    if (!our_category(category) || !key) return 0;
    if (!strcmp(key, PSVSD_ENABLE_KEY)) *option = PSVSD_ENABLED;
    else if (!strcmp(key, PSVSD_TARGET_KEY)) *option = PSVSD_TARGET;
    else return 0;
    return 1;
}

static int get_option(const char *category, const char *key, int *value) {
    PsvsdOption option;
    if (option_for_key(category, key, &option)) {
        int ret = psvsd_read_option(option, value);
        if (ret < 0) trace("Read %s failed: %08X\n", key, (unsigned)ret);
        return ret;
    }
    return TAI_CONTINUE(int, get_value.ref, category, key, value);
}

typedef struct { PsvsdOption option; int value; } OptionChange;

static int save_option_thread(SceSize size, void *data) {
    if (size != sizeof(OptionChange) || !data) return -1;
    const OptionChange *change = data;
    return psvsd_save_option(change->option, change->value);
}

static int set_option(const char *category, const char *key, int value) {
    PsvsdOption option;
    if (!option_for_key(category, key, &option)) return TAI_CONTINUE(int, set_value.ref, category, key, value);
    if (!psvsd_option_value_valid(option, value)) return -1;

    /* The storage checks use more stack than a Settings callback may have.
     * Wait for the commit before reporting a successful registry write. */
    SceUID thread = sceKernelCreateThread("PSVSDSettingsSave", save_option_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (thread < 0) {
        trace("Create save thread failed: %08X\n", (unsigned)thread);
        return thread;
    }
    OptionChange change = {option, value};
    int ret = sceKernelStartThread(thread, sizeof(change), &change);
    int saved = -1;
    if (ret >= 0) ret = sceKernelWaitThreadEnd(thread, &saved, NULL);
    int deleted = sceKernelDeleteThread(thread);
    if (ret < 0 || deleted < 0) {
        trace("Save thread: result=%08X delete=%08X\n", (unsigned)ret, (unsigned)deleted);
    }
    trace("Save %s=%d: %08X\n", key, value, (unsigned)(ret < 0 ? ret : saved));
    return ret < 0 ? ret : saved;
}

/* SceRegMgr's key metadata layout used by SceSystemSettingsCore. */
typedef struct { int size; const char *name; int type; int reserved; } RegistryKeyInfo;

static int get_option_info(const char *category, RegistryKeyInfo *info, int flags) {
    if (info && our_category(category)) {
        info->type = 0x00040000; /* Integer */
        return 0;
    }
    return TAI_CONTINUE(int, get_info.ref, category, info, flags);
}

typedef struct { char *xml; size_t size, insertion; } Layout;
static Layout layouts[4];
static unsigned char layout_busy;

static char *layout_with_page(const char *xml, size_t size, size_t insertion, int *fresh) {
    if (__atomic_test_and_set(&layout_busy, __ATOMIC_ACQUIRE)) return NULL;

    char *result = NULL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        Layout *layout = &layouts[i];
        if (!layout->xml) {
            result = malloc(size + psvsd_page_xml_size + 1);
            if (result) {
                memcpy(result, xml, insertion);
                memcpy(result + insertion, psvsd_page_xml, psvsd_page_xml_size);
                memcpy(result + insertion + psvsd_page_xml_size, xml + insertion, size - insertion);
                result[size + psvsd_page_xml_size] = '\0';
                *layout = (Layout){result, size, insertion};
                *fresh = 1;
            }
            break;
        }
        if (size == layout->size && insertion == layout->insertion &&
            !memcmp(layout->xml, xml, insertion) &&
            !memcmp(layout->xml + insertion + psvsd_page_xml_size, xml + insertion, size - insertion)) {
            result = layout->xml;
            break;
        }
    }
    __atomic_clear(&layout_busy, __ATOMIC_RELEASE);
    return result;
}

static int settings_layout(int context, void *xml, int size, int flags) {
    if (!page_active || !xml || size <= 0) {
        return TAI_CONTINUE(int, load_layout.ref, context, xml, size, flags);
    }
    PsvsdLayout layout;
    if (!psvsd_find_devices_layout(xml, (size_t)size, &layout)) {
        if ((size_t)size >= YAMT_PLUGIN_NAME_OFFSET + sizeof(DEVICES_PLUGIN) - 1 &&
            !memcmp((const char *)xml + YAMT_PLUGIN_NAME_OFFSET, DEVICES_PLUGIN, sizeof(DEVICES_PLUGIN) - 1)) {
            trace("Devices layout seen but opening tags could not be matched (%d bytes)\n", size);
        }
        return TAI_CONTINUE(int, load_layout.ref, context, xml, size, flags);
    }

    int fresh = 0;
    char *replacement = layout_with_page(xml, (size_t)size, layout.insertion, &fresh);
    if (!replacement) {
        trace("Devices layout buffer unavailable (%d bytes)\n", size);
        return TAI_CONTINUE(int, load_layout.ref, context, xml, size, flags);
    }
    int ret = TAI_CONTINUE(int, load_layout.ref, context, replacement, size + (int)psvsd_page_xml_size, flags);
    if (fresh || ret < 0) {
        trace("Devices page added: %d bytes, YAMT=%d, loader=%08X\n", size, layout.yamt_page, (unsigned)ret);
    }
    return ret;
}

static int detach_page(void) {
    page_active = 0;
    accessory_scene = NULL;
    int failed = remove_hook(&load_layout) < 0;
    failed |= remove_hook(&open_template) < 0;
    failed |= remove_hook(&open_page) < 0;
    failed |= remove_hook(&get_info) < 0;
    failed |= remove_hook(&set_value) < 0;
    failed |= remove_hook(&get_value) < 0;
    return failed;
}

static void attach_page(SceUID id) {
    if (page_active && core_id == id) return;
    core_id = id;
    if (add_hook(&load_layout, SETTINGS_MODULE, LIB_PAF_MISC, 0x19FE55A8u, settings_layout) < 0 ||
        add_hook(&get_value, SETTINGS_CORE, LIB_REGISTRY, 0x16DDF3DCu, get_option) < 0 ||
        add_hook(&set_value, SETTINGS_CORE, LIB_REGISTRY, 0xD72EA399u, set_option) < 0 ||
        add_hook(&get_info, SETTINGS_CORE, LIB_REGISTRY, 0x58421DD1u, get_option_info) < 0) {
        detach_page();
        return;
    }
    page_active = 1;
    int footer = psvsd_footer_init();
    if (footer < 0) {
        trace("Reboot Device PAF exports unavailable: %08X\n", (unsigned)footer);
    } else if (add_hook(&open_page, SETTINGS_CORE, LIB_PAF_TOPLEVEL, 0x30B442D9u, settings_page_open) < 0 ||
               add_hook(&open_template, SETTINGS_CORE, LIB_PAF_TOPLEVEL, 0xAB0DB9B0u, settings_template_open) < 0) {
        remove_hook(&open_template);
        remove_hook(&open_page);
    }
    trace("Settings hooks active for core %08X\n", (unsigned)id);
}

static SceUID core_loaded(const char *path, SceSize size, void *args, int flags, SceKernelLMOption *option, int *status) {
    SceUID id = TAI_CONTINUE(SceUID, load_core.ref, path, size, args, flags, option, status);
    const char *file = path ? strrchr(path, '/') : NULL;
    if (path && !strcmp(file ? file + 1 : path, SETTINGS_CORE_FILE)) {
        trace("Settings core load: %08X\n", (unsigned)id);
        if (id >= 0) attach_page(id);
    }
    return id;
}

static int core_unloaded(SceUID id, SceSize size, void *args, int flags, SceKernelULMOption *option, int *status) {
    int tracked = id >= 0 && id == core_id;
    if (tracked) detach_page();
    int ret = TAI_CONTINUE(int, unload_core.ref, id, size, args, flags, option, status);
    if (tracked) {
        core_id = -1;
        if (ret < 0) attach_page(id);
    }
    return ret;
}

int module_start(SceSize argc, const void *args);
int _start(SceSize argc, const void *args) __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    sceIoMkdir("ur0:data", 0777);
    sceIoMkdir(ACCESSORY_KERNEL_DIR, 0777);
    sceIoMkdir(AM_NATIVE_SETTINGS_DIR, 0777);
    int fd = sceIoOpen(SETTINGS_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) sceIoClose(fd);
    trace("PSVSD Native Settings " PSVSD_SETTINGS_VERSION " started\n");
    if (add_hook(&load_core, SETTINGS_MODULE, LIB_KERNEL, 0x2DCC4AFAu, core_loaded) < 0 ||
        add_hook(&unload_core, SETTINGS_MODULE, LIB_KERNEL, 0x2415F8A4u, core_unloaded) < 0) {
        return remove_hook(&load_core) < 0 ? SCE_KERNEL_START_SUCCESS : SCE_KERNEL_START_NO_RESIDENT;
    }
    tai_module_info_t info = {.size = sizeof(info)};
    if (taiGetModuleInfo(SETTINGS_CORE, &info) >= 0) attach_page(info.modid);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    if (psvsd_footer_in_use()) return SCE_KERNEL_STOP_CANCEL;
    int failed = detach_page();
    failed |= remove_hook(&unload_core) < 0;
    failed |= remove_hook(&load_core) < 0;
    if (failed) return SCE_KERNEL_STOP_CANCEL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        free(layouts[i].xml);
        layouts[i] = (Layout){0};
    }
    return SCE_KERNEL_STOP_SUCCESS;
}
