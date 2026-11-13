/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "footer.h"
#include "layout.h"
#include <stddef.h>
#include <string.h>
#include <taihen.h>

/* The Settings resource IDs are hashes of names in system_settings_main.rco. */
#define SETTINGS_LIST_TEMPLATE 0xA7851827u
#define FOOTER_BUTTON_TEMPLATE 0xDC49520Bu
#define FOOTER_BUTTON_WIDGET 0x4AB91AD2u
#define ACCESSORY_PAGE_HASH 0x4D9BC58Cu

#define PAF_TOPLEVEL 0x4D9A9DD0u
#define PAF_WIDGET 0x073F8C68u
#define PAF_STDC 0xA7D28DAEu
#define BUTTON_DECIDE 0x10000008
#define PAF_CENTER 0u
#define LISTENER_KILLED 1u
#define LISTENER_AUTODELETE 2u
#define ANIM_MOVE 0x10000
#define ANIM_META_ALPHA 0x10003
#define ANIM_SIZE 0x10004
#define BUTTON_LABEL_OBJECT 1

struct PsvsdPafTemplateParams { void *init_events, *sibling; };
typedef struct { const uint16_t *text; uint32_t length; } PafWideString;
typedef struct { _Alignas(16) float x; float y, z, w; } PafVector;
typedef void (*PafAnimationDone)(void *, void *, void *, void *);
typedef int (*RebootAction)(void);
typedef void (*PafEventCallback)(int, void *, void *, RebootAction);

typedef struct EventListener EventListener;
typedef struct {
    void (*destroy)(EventListener *);
    void (*dispose)(EventListener *);
    int (*dispatch)(EventListener *, int, void *, void *);
    PafEventCallback (*callback)(EventListener *);
} EventListenerMethods;

/* EventCBListener layout used by PAF. PAF owns the listener after registration. */
struct EventListener {
    const EventListenerMethods *methods;
    unsigned char flags;
    RebootAction reboot;
    PafEventCallback callback;
};

static struct {
    int (*open_template)(void *, void *, const PsvsdPafId *, const PsvsdPafTemplateParams *);
    void *(*find_child)(void *, const PsvsdPafId *, int);
    int (*set_label)(void *, const PafWideString *, int);
    const PafVector *(*get_size)(void *, int);
    int (*set_align)(void *, unsigned, unsigned, unsigned, void *);
    int (*set_anchor)(void *, unsigned, unsigned, unsigned, void *);
    int (*set_pos)(void *, const PafVector *, void *, int, int, float, PafAnimationDone, void *);
    int (*set_size)(void *, const PafVector *, void *, int, int, float, PafAnimationDone, void *);
    int (*set_meta_alpha)(void *, float, void *, int, int, float, PafAnimationDone, void *);
    int (*add_listener)(void *, int, EventListener *, int);
    void (*enable)(void *, int);
    void (*disable)(void *, int);
    void *(*allocate)(size_t);
    void (*release)(void *);
} paf;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(PsvsdPafId) == 16 && offsetof(PsvsdPafId, hash) == 12, "PAF ID layout");
_Static_assert(sizeof(EventListener) == 16 && offsetof(EventListener, callback) == 12, "PAF listener layout");
_Static_assert(sizeof(PafVector) == 16 && sizeof(PafWideString) == 8, "PAF value layout");
#endif

static unsigned live_listeners;

static void destroy_listener(EventListener *listener) {
    (void)listener;
    --live_listeners;
}

static void delete_listener(EventListener *listener) {
    destroy_listener(listener);
    paf.release(listener);
}

static int dispatch_listener(EventListener *listener, int type, void *widget, void *event) {
    if (listener->flags & LISTENER_KILLED) return -1;
    listener->callback(type, widget, event, listener->reboot);
    return 0;
}

static PafEventCallback listener_callback(EventListener *listener) { return listener->callback; }

static const EventListenerMethods listener_methods = {
    destroy_listener, delete_listener, dispatch_listener, listener_callback,
};

static void reboot_pressed(int type, void *widget, void *event, RebootAction reboot) {
    (void)event;
    if (type == BUTTON_DECIDE && reboot() >= 0) paf.disable(widget, 0);
}

int psvsd_footer_init(void) {
    if (paf.release) return 0;
#define LOAD_PAF(field, lib, nid) do {                                      \
    uintptr_t address = 0;                                                  \
    int result = taiGetModuleExportFunc("ScePaf", lib, nid, &address);         \
    if (result < 0 || !address) return result < 0 ? result : -1;              \
    paf.field = (__typeof__(paf.field))address;                              \
} while (0)
    LOAD_PAF(open_template, PAF_TOPLEVEL, 0xAB0DB9B0u);
    LOAD_PAF(find_child, PAF_WIDGET, 0x9D304F4Au);
    LOAD_PAF(set_label, PAF_WIDGET, 0xD149484Au);
    LOAD_PAF(get_size, PAF_WIDGET, 0x2F924BADu);
    LOAD_PAF(set_align, PAF_WIDGET, 0x5F04A98Eu);
    LOAD_PAF(set_anchor, PAF_WIDGET, 0x1BC5EED6u);
    LOAD_PAF(set_pos, PAF_WIDGET, 0xE29AB31Fu);
    LOAD_PAF(set_size, PAF_WIDGET, 0x046AA07Du);
    LOAD_PAF(set_meta_alpha, PAF_WIDGET, 0x543F0959u);
    LOAD_PAF(add_listener, PAF_WIDGET, 0xFB7FE189u);
    LOAD_PAF(enable, PAF_WIDGET, 0x8F1A3A59u);
    LOAD_PAF(disable, PAF_WIDGET, 0xA1173889u);
    LOAD_PAF(allocate, PAF_STDC, 0xFC5CD359u);
    LOAD_PAF(release, PAF_STDC, 0x1B77082Eu);
#undef LOAD_PAF
    return 0;
}

int psvsd_footer_is_accessory_page(const PsvsdPafId *id) {
    return id && id->hash == ACCESSORY_PAGE_HASH && id->text &&
           !strcmp(id->text, PSVSD_PAGE_ID);
}

int psvsd_footer_is_list_template(const PsvsdPafId *id) {
    return id && id->hash == SETTINGS_LIST_TEMPLATE;
}

static void hide_button(void *button) {
    paf.disable(button, 0);
    paf.set_meta_alpha(button, 0.0f, NULL, ANIM_META_ALPHA, 0, 0.0f, NULL, NULL);
}

int psvsd_footer_add(void *plugin, void *scene, RebootAction reboot) {
    static const PsvsdPafId template = {"", 0, 0, FOOTER_BUTTON_TEMPLATE};
    static const PsvsdPafId widget_id = {"", 0, 0, FOOTER_BUTTON_WIDGET};
    static const uint16_t label_text[] = u"Reboot Device";
    static const PafWideString label = {label_text, sizeof(label_text) / sizeof(label_text[0]) - 1};
    static const PafVector size = {220.0f, 48.0f, 0.0f, 0.0f};
    const float margin = 24.0f;
    const PafVector *page_size = paf.get_size(scene, 0);
    if (!page_size || page_size->x < size.x + margin || page_size->y < size.y + margin) return -1;
    /* PAF positions are relative to the parent's center, with positive Y up. */
    const PafVector position = {(page_size->x - size.x) * 0.5f - margin,
                                (size.y - page_size->y) * 0.5f + margin, 0.0f, 0.0f};
    const PsvsdPafTemplateParams params = {0};

    EventListener *listener = paf.allocate(sizeof(*listener));
    if (!listener) return -1;
    int ret = paf.open_template(plugin, scene, &template, &params);
    void *button = ret >= 0 ? paf.find_child(scene, &widget_id, 2) : NULL;
    if (!button) {
        paf.release(listener);
        return ret < 0 ? ret : -1;
    }
    ret = paf.set_label(button, &label, BUTTON_LABEL_OBJECT);
    if (ret >= 0) ret = paf.set_size(button, &size, NULL, ANIM_SIZE, 0, 0.0f, NULL, NULL);
    if (ret >= 0) ret = paf.set_align(button, PAF_CENTER, PAF_CENTER, PAF_CENTER, NULL);
    if (ret >= 0) ret = paf.set_anchor(button, PAF_CENTER, PAF_CENTER, PAF_CENTER, NULL);
    if (ret >= 0) ret = paf.set_pos(button, &position, NULL, ANIM_MOVE, 0, 0.0f, NULL, NULL);
    if (ret < 0) {
        hide_button(button);
        paf.release(listener);
        return ret;
    }
    *listener = (EventListener){&listener_methods, LISTENER_AUTODELETE, reboot, reboot_pressed};
    ++live_listeners;
    /* Replace any default Decide listener on this new widget only. */
    ret = paf.add_listener(button, BUTTON_DECIDE, listener, 1);
    if (ret < 0) {
        hide_button(button);
        return ret;
    }
    paf.enable(button, 1);
    return 0;
}

int psvsd_footer_in_use(void) { return live_listeners != 0; }
