/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "install.h"
#include <limits> // Required by the VitaSDK PAF vector header.
#include <paf.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/registrymgr.h>
#include <psp2/system_param.h>
#include <psp2/sysmodule.h>
#include <atomic>
#include <cstdio>
#include <new>

// PAF owns the event listeners created by Widget::SetEventCallback.
void *operator new(size_t size) { return sce_paf_malloc(size); }
void operator delete(void *pointer) noexcept { sce_paf_free(pointer); }
void operator delete(void *pointer, size_t) noexcept { sce_paf_free(pointer); }

extern "C" {
SceSize sceUserMainThreadStackSize = 0x10000;
}

enum class Action { Install, Uninstall, Reboot, Back };
enum class Activity { Idle, Busy, Rebooting, Exiting };
static struct MenuItem {
    const char *id;
    const wchar_t *label;
    Action action;
} menu[] = {
    {"install_plugin", L"Install Accessory Mount Plugin", Action::Install},
    {"uninstall_plugin", L"Uninstall", Action::Uninstall},
    {"reboot_device", L"Reboot", Action::Reboot},
    {"exit_installer", nullptr, Action::Back},
};
static paf::ui::Widget *status;
static bool ready;
static std::atomic<Activity> activity{Activity::Idle};

static void show_status(const char *message) {
    wchar_t wide[240];
    size_t i = 0;
    while (message[i] && i < sizeof(wide) / sizeof(*wide) - 1) {
        wide[i] = static_cast<unsigned char>(message[i]);
        ++i;
    }
    wide[i] = 0;
    status->SetString(paf::wstring(wide), paf::ui::Text::OBJ_TEXT);
}

static void activate(int, paf::ui::Handler *, paf::ui::Event *, void *data) {
    Action action = *static_cast<Action *>(data);
    Activity idle = Activity::Idle;
    if (!activity.compare_exchange_strong(idle, Activity::Busy)) return;
    if (action == Action::Back) {
        sceKernelExitProcess(0);
        activity = Activity::Idle;
        return;
    }
    char message[240];
    if (action == Action::Reboot) {
        int ret = amp_prepare_reboot(message, sizeof(message));
        if (ret < 0) {
            show_status(message);
            activity = Activity::Idle;
            return;
        }
        ret = scePowerRequestColdReset();
        if (ret < 0) {
            std::snprintf(message, sizeof(message), "Reboot failed (%08X).", static_cast<unsigned>(ret));
            show_status(message);
            activity = Activity::Idle;
        } else {
            activity = Activity::Rebooting;
            show_status("Rebooting...");
        }
        return;
    }
    sceKernelPowerLock(SCE_KERNEL_POWER_TICK_DEFAULT);
    int ret = amp_change_installation(action == Action::Install, message, sizeof(message));
    sceKernelPowerUnlock(SCE_KERNEL_POWER_TICK_DEFAULT);
    if (ret < 0) sceClibPrintf("Accessory Mount Plugin: %08X\n", static_cast<unsigned>(ret));
    show_status(message);
    activity = Activity::Idle;
}

static void load_menu(paf::Plugin *plugin) {
    if (!plugin) return;
    paf::Plugin::PageOpenParam parameters;
    parameters.option = paf::Plugin::PageOption_None;
    paf::ui::Scene *scene = plugin->PageOpen("accessory_mount_page", parameters);
    if (!scene) return;
    auto *root = scene->FindChild("installer_root");
    if (!root) return;
    status = root->FindChild("operation_status");
    auto *title = root->FindChild("installer_title");
    if (!title || !status) return;
    title->SetString(paf::wstring(L"Accessory Mount Plugin"), paf::ui::Text::OBJ_TEXT);
    for (auto &item : menu) {
        auto *button = root->FindChild(item.id);
        if (!button) return;
        if (item.label) button->SetString(paf::wstring(item.label), paf::ui::Button::OBJ_LABEL);
        if (button->SetEventCallback(paf::ui::ButtonBase::CB_BTN_DECIDE, activate, &item.action) < 0) return;
    }
    char message[240];
    if (amp_finish_pending_installation(message, sizeof(message)) < 0) show_status(message);
    ready = true;
}

static int back_button(SceSize size, void *data) {
    if (size != sizeof(unsigned)) return -1;
    unsigned cancel = *static_cast<unsigned *>(data);
    bool released = false;
    while (activity != Activity::Rebooting) {
        SceCtrlData pad = {};
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            if (!(pad.buttons & cancel)) released = true;
            else if (released) {
                released = false;
                Activity idle = Activity::Idle;
                if (activity.compare_exchange_strong(idle, Activity::Exiting)) {
                    sceKernelExitProcess(0);
                    activity = Activity::Idle;
                }
            }
        }
        sceKernelDelayThread(16000);
    }
    return 0;
}

int main() {
    // Standalone PAF framework initialization uses the stock Vita resources.
    struct PafInit {
        unsigned global_heap_size, worker_priority, worker_stack, dialog_mode, heap_options[2];
    } arguments = {16 * 1024 * 1024, 0xEA60, 0x40000, 0, {0, 0}};
    int result = 0;
    SceSysmoduleOpt load = {};
    load.result = &result;
    int ret = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(arguments), &arguments, &load);
    if (ret < 0 || result < 0) {
        sceClibPrintf("Accessory Mount Plugin: PAF init %08X / %08X\n", static_cast<unsigned>(ret), static_cast<unsigned>(result));
        return 1;
    }

    int enter = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    if (sceRegMgrSystemParamGetInt(SCE_REGMGR_SYSTEM_PARAM_ID_BUTTON_ASSIGN, &enter) < 0 ||
        (enter != SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE && enter != SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS))
        enter = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    paf::Framework::InitParam framework_parameters;
    framework_parameters.mode = paf::Framework::Mode_Normal;
    framework_parameters.allow_button_control = true;
    framework_parameters.enter_button_assign = static_cast<unsigned>(enter);
    void *memory = sce_paf_malloc(sizeof(paf::Framework));
    if (!memory) return 1;
    auto *framework = new (memory) paf::Framework(framework_parameters);
    framework->LoadCommonResourceSync();
    sceAppMgrSetInfobarState(SCE_APPMGR_INFOBAR_VISIBILITY_VISIBLE, SCE_APPMGR_INFOBAR_COLOR_BLACK,
                            SCE_APPMGR_INFOBAR_TRANSPARENCY_OPAQUE);

    paf::Plugin::InitParam plugin_parameters;
    plugin_parameters.name = "accessory_mount_installer";
    plugin_parameters.caller_name = "__main__";
    plugin_parameters.resource_file = "app0:/installer_menu.rco";
    plugin_parameters.start_func = load_menu;
    paf::Plugin::LoadSync(plugin_parameters);
    if (!ready) {
        sceClibPrintf("Accessory Mount Plugin: menu initialization failed\n");
        return 1;
    }

    unsigned cancel = enter == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE ? SCE_CTRL_CROSS : SCE_CTRL_CIRCLE;
    SceUID thread = sceKernelCreateThread("AccessoryMountBack", back_button, 0x10000100, 0x4000, 0, 0, nullptr);
    if (thread >= 0) sceKernelStartThread(thread, sizeof(cancel), &cancel);
    return framework->Run();
}
