/* Minimal hilog shim: lets the HarmonyOS guest runtime build and run on a
 * plain POSIX host. Only OH_LOG_Print is used by the runtime.
 *
 * The project's format strings use hilog's privacy qualifiers (%{public}s,
 * %{public}08x, %{private}u, ...). Plain printf would print those literally,
 * so strip the qualifier before formatting: %{public}X becomes %X.
 *
 * Used via -I <this dir>, so project sources stay untouched. */
#ifndef OHOS_HILOG_COMPAT_H
#define OHOS_HILOG_COMPAT_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    LOG_DEBUG = 3,
    LOG_INFO = 4,
    LOG_WARN = 5,
    LOG_ERROR = 6,
    LOG_FATAL = 7
} LogLevel;

#define LOG_APP 0
#define LOG_CORE 1

/* Strip hilog privacy qualifiers; bounded, never fails hard. */
static inline const char *ohos_strip_qualifiers(const char *fmt, char *buf,
                                                size_t cap) {
    size_t o = 0;
    for (const char *p = fmt; *p != '\0' && o + 1 < cap; ) {
        if (p[0] == '%' && p[1] == '{') {
            const char *end = strchr(p + 2, '}');
            if (end != NULL) {
                buf[o++] = '%';
                p = end + 1;
                continue;
            }
        }
        buf[o++] = *p++;
    }
    buf[o] = '\0';
    return buf;
}

static inline int OH_LOG_Print(int type, int level, unsigned int domain,
                               const char *tag, const char *fmt, ...) {
    char clean[4096];
    FILE *out = (level >= LOG_ERROR) ? stderr : stdout;
    va_list ap;
    (void)type;
    (void)domain;
    fprintf(out, "[%s] ", tag ? tag : "?");
    va_start(ap, fmt);
    int n = vfprintf(out, ohos_strip_qualifiers(fmt, clean, sizeof clean), ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
    return n;
}

#endif
