#ifndef PSVSD_AM_MANAGER_H
#define PSVSD_AM_MANAGER_H
#include "../common/paths.h"
#include "../common/settings.h"
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int ux0,uma0,raw_usb,gamecard,loaded,legacy_loaded;
    int actual_target,verified_mount,original_ready;
    int module_id;
} AmStatus;
typedef int (*AmStorageRequest)(void *ctx,char *message,size_t cap);
typedef struct {
    AmStorageRequest request_usb;
    void *ctx;
    AmStorageRequest request_original;
} AmSaveCallbacks;
int am_read_text(const char *path,char *out,size_t cap);
int am_load_settings(AmSettings *s);
int am_save_settings_with_callbacks(const AmSettings *s,const AmSaveCallbacks *callbacks,char *message,size_t cap);
void am_status(AmStatus *s);
void am_expect_query(uint32_t token);
const char *am_parse_query_header(const char *report,uint32_t *token);
int am_stage_status(void);
void am_log(const char *fmt,...);
#endif
