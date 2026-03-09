#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static log_level_t g_min_level = LOG_INFO;

void log_init(int verbose)
{
    g_min_level = verbose ? LOG_DEBUG : LOG_INFO;
}

void log_msg(log_level_t level, const char *fmt, ...)
{
    if (level < g_min_level) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char tbuf[24];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);

    static const char *level_str[] = { "DBG", "INF", "WRN", "ERR" };
    FILE *out = (level >= LOG_WARN) ? stderr : stdout;

    fprintf(out, "[%s.%03ld] [%s] ", tbuf, ts.tv_nsec / 1000000L, level_str[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fprintf(out, "\n");
    fflush(out);
}
