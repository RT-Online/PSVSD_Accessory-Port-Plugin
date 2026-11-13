/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "layout.h"
#include "../common/settings.h"
#include <string.h>

const char psvsd_page_xml[] =
    "\n    <setting_list id=\"" PSVSD_PAGE_ID "\" title=\"PSVSD Accessory Port\" icon=\"tex_spanner\">\n"
    "      <toggle_switch id=\"psvsd_enable\" key=\"" PSVSD_CATEGORY "/" PSVSD_ENABLE_KEY "\"\n"
    "                     title=\"Enable Accessory Port\"/>\n"
    "      <list id=\"psvsd_mount\" key=\"" PSVSD_CATEGORY "/" PSVSD_TARGET_KEY "\" title=\"Mount as\"\n"
    "            description=\"Applies on the next reboot.\">\n"
#define AM_MOUNT(symbol, value, stem, mount_id) \
    "        <list_item id=\"psvsd_mount_" #stem "\" title=\"" #stem ":\" value=\"" #value "\"/>\n"
#include "../common/mounts.def"
#undef AM_MOUNT
    "      </list>\n"
    "    </setting_list>\n";
const size_t psvsd_page_xml_size = sizeof(psvsd_page_xml) - 1;

typedef struct {
    const char *start;
    size_t length;
} Span;

typedef struct {
    Span name, id, plugin;
    int close, empty;
} Tag;

static int matches(Span span, const char *value) {
    return span.length == strlen(value) && !memcmp(span.start, value, span.length);
}

static int space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void skip_space(const char **cursor, const char *end) {
    while (*cursor < end && space(**cursor)) ++*cursor;
}

static Span read_name(const char **cursor, const char *end) {
    const char *start = *cursor;
    while (*cursor < end && !space(**cursor) && !strchr("<>=/'\"", **cursor)) ++*cursor;
    return (Span){start, (size_t)(*cursor - start)};
}

static const char *after_marker(const char *p, const char *end, const char *marker, size_t length) {
    for (; (size_t)(end - p) >= length; ++p) {
        if (!memcmp(p, marker, length)) return p + length;
    }
    return NULL;
}

/* The Settings resources use ordinary elements, comments and processing instructions. */
static const char *read_tag(const char *p, const char *end, Tag *tag) {
    *tag = (Tag){0};
    if (p == end) return NULL;
    if (*p == '/') { tag->close = 1; ++p; }
    tag->name = read_name(&p, end);
    if (!tag->name.length) return NULL;
    skip_space(&p, end);
    while (p < end) {
        if (*p == '>') return p + 1;
        if (tag->close) return NULL;
        if (*p == '/' && end - p >= 2 && p[1] == '>') {
            tag->empty = 1;
            return p + 2;
        }
        Span attr = read_name(&p, end);
        skip_space(&p, end);
        if (!attr.length || p == end || *p++ != '=') return NULL;
        skip_space(&p, end);
        if (p == end || (*p != '"' && *p != '\'')) return NULL;
        char quote = *p++;
        const char *stop = memchr(p, quote, (size_t)(end - p));
        if (!stop || memchr(p, '<', (size_t)(stop - p))) return NULL;
        Span value = {p, (size_t)(stop - p)};
        if (matches(attr, "id")) tag->id = value;
        if (matches(attr, "plugin")) tag->plugin = value;
        p = stop + 1;
        skip_space(&p, end);
    }
    return NULL;
}

int psvsd_find_devices_layout(const char *xml, size_t length, PsvsdLayout *layout) {
    if (!xml || !length || length > PSVSD_LAYOUT_LIMIT) return 0;
    const char *end = xml + length;
    while (end > xml && !end[-1]) --end;
    if (memchr(xml, '\0', (size_t)(end - xml))) return 0;

    const char *p = xml;
    PsvsdLayout result = {0};
    unsigned depth = 0;
    int finished = 0, in_devices = 0;
    while ((p = memchr(p, '<', (size_t)(end - p))) != NULL) {
        ++p;
        if (end - p >= 3 && !memcmp(p, "!--", 3)) {
            p = after_marker(p + 3, end, "-->", 3);
            if (!p) return 0;
            continue;
        }
        if (p < end && *p == '?') {
            p = after_marker(p + 1, end, "?>", 2);
            if (!p) return 0;
            continue;
        }
        if (finished || (p < end && *p == '!')) return 0;

        Tag tag;
        p = read_tag(p, end, &tag);
        if (!p || matches(tag.id, PSVSD_PAGE_ID)) return 0;
        if (tag.close) {
            if (!depth) return 0;
            if (depth == 2) in_devices = 0;
            if (--depth == 0) {
                if (!matches(tag.name, "system_settings")) return 0;
                finished = 1;
            }
            continue;
        }
        if (depth == 0 && (tag.empty || !matches(tag.name, "system_settings") ||
                           !matches(tag.plugin, "peripherals_settings_plugin"))) return 0;
        if (depth == 1 && matches(tag.name, "setting_list") && matches(tag.id, "peripherals_settings")) {
            if (result.insertion || tag.empty) return 0;
            result.insertion = (size_t)(p - xml);
            in_devices = 1;
        }
        if (depth == 2 && in_devices && matches(tag.name, "setting_list") && matches(tag.id, "id_yamt")) result.yamt_page = 1;
        if (!tag.empty) ++depth;
    }
    if (!finished || !result.insertion) return 0;
    *layout = result;
    return 1;
}
