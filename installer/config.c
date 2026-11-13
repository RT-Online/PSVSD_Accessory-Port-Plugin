/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "../src/common/paths.h"
#include <string.h>

static int whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int equals(const char *s, size_t length, const char *literal) {
    if (length != strlen(literal)) return 0;
    for (size_t i = 0; i < length; ++i)
        if (lower(s[i]) != lower(literal[i])) return 0;
    return 1;
}

static int append(char **out, size_t *space, const char *bytes, size_t count) {
    if (count >= *space) return -1;
    memcpy(*out, bytes, count);
    *out += count;
    *space -= count;
    **out = '\0';
    return 0;
}

int amp_config(const char *input, size_t length, char *output, size_t capacity,
               int install, int remove_boot, AmpConfig *current) {
    if (!capacity || memchr(input, '\0', length)) return -1;
    *current = (AmpConfig){0};
    *output = '\0';
    char *out = output;
    const char *limit = input + length;
    const char *first_newline = memchr(input, '\n', length);
    const char *eol = first_newline && first_newline > input && first_newline[-1] == '\r' ? "\r\n" : "\n";
    size_t eol_length = strlen(eol);
    int kernel = 0, inserted = 0;
    const char *line = input;
    /* A text editor can preserve a UTF-8 BOM. Keep it with the first line. */
    if (length >= 3 && !memcmp(input, "\xef\xbb\xbf", 3)) {
        if (append(&out, &capacity, input, 3) < 0) return -1;
        line += 3;
    }
    const char *start = line;
    while (line < limit) {
        const char *newline = memchr(line, '\n', (size_t)(limit - line));
        const char *end = newline ? newline : limit;
        const char *next = newline ? end + 1 : end;
        while (end > line && whitespace(end[-1])) --end;
        const char *text = line;
        while (text < end && whitespace(*text)) ++text;
        const int marker = equals(text, (size_t)(end - text), "# BEGIN PSVSD/AM") ||
                           equals(text, (size_t)(end - text), "# END PSVSD/AM");
        const char *comment = memchr(text, '#', (size_t)(end - text));
        if (comment) end = comment;
        while (end > text && whitespace(end[-1])) --end;
        size_t size = (size_t)(end - text);
        int settings_header = equals(text, size, "*NPXS10015");
        if (size && *text == '*') kernel = equals(text, size, "*KERNEL");
        int settings_entry = equals(text, size, AMP_SETTINGS_PLUGIN);
        int boot_entry = kernel && equals(text, size, AM_BOOT_PLUGIN);
        current->has_settings |= settings_entry;
        current->has_boot |= boot_entry;
        if (!settings_entry && !(remove_boot && kernel && (boot_entry || marker))) {
            if (append(&out, &capacity, line, (size_t)(next - line)) < 0) return -1;
        }
        if (install && settings_header && !inserted) {
            if (!newline && append(&out, &capacity, eol, eol_length) < 0) return -1;
            if (append(&out, &capacity, AMP_SETTINGS_PLUGIN, sizeof(AMP_SETTINGS_PLUGIN) - 1) < 0 ||
                append(&out, &capacity, eol, eol_length) < 0) return -1;
            inserted = 1;
        }
        line = next;
    }
    if (install && !inserted) {
        static const char header[] = "*NPXS10015";
        if (start < limit && out > output && out[-1] != '\n' &&
            append(&out, &capacity, eol, eol_length) < 0) return -1;
        if (append(&out, &capacity, header, sizeof(header) - 1) < 0 ||
            append(&out, &capacity, eol, eol_length) < 0 ||
            append(&out, &capacity, AMP_SETTINGS_PLUGIN, sizeof(AMP_SETTINGS_PLUGIN) - 1) < 0 ||
            append(&out, &capacity, eol, eol_length) < 0) return -1;
    }
    return (int)(out - output);
}
