#define _POSIX_C_SOURCE 199309L

#include "triSeek/include/triseek/common.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

uint64_t ts_now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

void ts_log(const char *level, const char *fmt, ...) {
    struct timespec now;
    struct tm local;
    char timestamp[32];
    va_list args;

    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    fprintf(stderr, "%s.%03ld [%s] ", timestamp, now.tv_nsec / 1000000L, level);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}   