#ifndef TRISEEK_PROTOCOL_H
#define TRISEEK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define TS_PROTOCOL_MAGIC 0x5453454BU
#define TS_PROTOCOL_VERSION 1U
#define TS_PROTOCOL_HEADER_SIZE 

typedef enum {
    TS_MSG_QUERY_REQUEST = 1,
    TS_MSG_QUERY_ACCEPTED = 2,
    TS_MSG_MATCH_RESULT = 3,
    TS_MSG_QUERY_COMPLETE = 4,
    TS_MSG_ERROR_RESPONSE = 5
} ts_message_type_t;

typedef struct {
    uint16_t type;
    uint32_t flags;
    uint64_t request_id;
    uint32_t body_length;
    uint8_t *body;
} ts_message_t;

#endif
