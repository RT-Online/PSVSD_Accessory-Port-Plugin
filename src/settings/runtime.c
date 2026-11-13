/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Use the Settings process's SceLibKernel exports. A native plugin does
 * not start a homebrew application C runtime. */
typedef union { SceUID block; max_align_t alignment; } Allocation;

void *malloc(size_t size) {
    const size_t page = 0x1000;
    if (size > SIZE_MAX - sizeof(Allocation) - (page - 1)) return NULL;
    size_t bytes = (size + sizeof(Allocation) + page - 1) & ~(page - 1);
    SceUID block = sceKernelAllocMemBlock("PSVSDSettingsBuffer", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, bytes, NULL);
    if (block < 0) return NULL;
    void *base;
    if (sceKernelGetMemBlockBase(block, &base) < 0) {
        sceKernelFreeMemBlock(block);
        return NULL;
    }
    Allocation *allocation = base;
    allocation->block = block;
    return allocation + 1;
}

void free(void *memory) {
    if (memory) sceKernelFreeMemBlock(((Allocation *)memory - 1)->block);
}

int vsnprintf(char *out, size_t size, const char *format, va_list args) {
    return sceClibVsnprintf(out, size, format, args);
}

int snprintf(char *out, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = sceClibVsnprintf(out, size, format, args);
    va_end(args);
    return ret;
}

void *memset(void *out, int c, size_t size) { return sceClibMemset(out, c, size); }
void *memcpy(void *out, const void *in, size_t size) { return sceClibMemcpy(out, in, size); }
int memcmp(const void *a, const void *b, size_t size) { return sceClibMemcmp(a, b, size); }
void *memchr(const void *p, int c, size_t size) { return sceClibMemchr(p, c, size); }
int strcmp(const char *a, const char *b) { return sceClibStrcmp(a, b); }
int strncmp(const char *a, const char *b, size_t size) { return sceClibStrncmp(a, b, size); }
size_t strlen(const char *text) {
    const char *end = text;
    while (*end) ++end;
    return (size_t)(end - text);
}
char *strchr(const char *text, int c) { return sceClibStrchr(text, c); }
char *strrchr(const char *text, int c) { return sceClibStrrchr(text, c); }
char *strstr(const char *text, const char *needle) { return sceClibStrstr(text, needle); }
