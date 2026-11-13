#include "settings.h"
#include <string.h>
#include <stdio.h>
static const struct { const char *name; int id; } mounts[AM_TARGET_COUNT] = {
#define AM_MOUNT(symbol, value, stem, mount_id) [AM_##symbol] = {#stem ":", mount_id},
#include "mounts.def"
#undef AM_MOUNT
};

const char *am_target_name(int target) {
    return (unsigned)target < AM_TARGET_COUNT ? mounts[target].name : "none";
}

int am_target_mount_id(int target) {
    return (unsigned)target < AM_TARGET_COUNT ? mounts[target].id : -1;
}

int am_target_from_name(const char *name, size_t length) {
    for (int target=0; target<AM_TARGET_COUNT; ++target) {
        const char *alias=mounts[target].name;
        size_t n=strlen(alias);
        if ((length==n || length==n-1) && !memcmp(name,alias,length)) return target;
    }
    return -1;
}
int am_settings_parse(const char *text, AmSettings *out) {
    AmSettings result={0,AM_UMA0};unsigned seen=0;
    int retired_target=0;
    while (*text) {
        const char *end=strchr(text,'\n');size_t n=end ? (size_t)(end-text) : strlen(text);
        while (n && (text[n-1]=='\r' || text[n-1]==' ' || text[n-1]=='\t')) --n;
        while (n && (*text==' ' || *text=='\t')) { ++text;--n; }
        unsigned field=0;
        if (!n || *text=='#') { /* comments */ }
        else if (n==9 && !memcmp(text,"version=1",9)) field=1;
        else if (n==9 && !memcmp(text,"enabled=",8) && (text[8]=='0' || text[8]=='1')) {
            field=2;result.enabled=text[8]-'0';
        } else if (n>6 && !memcmp(text,"mount=",6)) {
            const char *mount=text+6;
            size_t length=n-6;
            int target=am_target_from_name(mount,length);
            if (target>=0) result.target=target;
            else if ((length==4 || (length==5 && mount[4]==':')) && !memcmp(mount,"xmc0",4)) retired_target=1;
            else return -1;
            field=4;
        }
        else return -1;
        if (field && (seen&field)) return -1;
        seen|=field;if (!end) break;text=end+1;
    }
    if (seen!=7) return -1;
    /* 0.1.2 could save xmc0. Open that configuration with mounting disabled. */
    if (retired_target) result.enabled=0;
    *out=result;return 0;
}
int am_settings_format(const AmSettings *s,char *out,size_t cap) {
    if ((s->enabled!=0 && s->enabled!=1) || (unsigned)s->target>=AM_TARGET_COUNT) return -1;
    const char *alias=am_target_name(s->target);
    int n=snprintf(out,cap,"# PSVSD/AM\nversion=1\nenabled=%d\nmount=%.*s\n",s->enabled,(int)strlen(alias)-1,alias);
    return n>=0 && (size_t)n<cap ? n : -1;
}
