#ifndef TRISEEK_COMMON_H
#define TRESEEK_COMMON_H

#include <stdint.h>

#define TS_DEFAULT_PORT 9000
#define TS_DEFAULT_BUFFER_INITIAL_CAPACITY 256
#define TS_DEFAULT_BUFFER_MAX_CAPACITY 1024 * 64

typedef enum{
    TS_OK = 0,

    TS_ERR_INVALID = -1,
    TS_ERR_NOMEM = -2,
    TS_ERR_IO = -3,
    TS_ERR_PROTOCOL = -4,
    TS_ERR_CLOSED = -5,
    TS_ERR_CANCELLED = -6,
    TS_ERR_TIMEOUT = -7,
    TS_ERR_NOT_FOUND = -8,

    TS_ERR_UNKNOW = -99
} ts_status_t;

uint64_t ts_now_ms(void);

#define TS_LOG_INFO(...) ts_log("INFO",__VA__ARGS__)
#define TS_LOG_WARN(...) ts_log("WARN",__VA__ARGS__)
#define TS_LOG_ERROR(...) ts_log("ERROR",__VA__ARGS__)

# endif