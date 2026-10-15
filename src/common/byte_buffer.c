#include "triSeek/include/triseek/byte_buffer.h"
#include "triSeek/include/triseek/common.h"

#include <stdlib.h>
#include <string.h>

int ts_buffer_init(ts_byte_buffer_t *buffer, size_t initial_capacity, size_t max_capacity) {
    if (buffer == NULL)
        return TS_ERR_INVALID;
    if (initial_capacity == 0)
        initial_capacity = TS_DEFAULT_BUFFER_INITIAL_CAPACITY;
    buffer->data = malloc(initial_capacity);
    if (buffer->data == NULL)
        return TS_ERR_NOMEM;
    buffer->capacity = initial_capacity;
    buffer->read_pos = 0;
    buffer->write_pos = 0;
    buffer->max_capacity = max_capacity;
    return TS_OK;
}

void ts_buffer_destroy(ts_byte_buffer_t *buffer) {
    if (buffer == NULL)
        return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

int ts_buffer_append(ts_byte_buffer_t *buffer, const void *data, size_t length) {
    if (buffer == NULL || data == NULL)
        return TS_ERR_INVALID;
    if (length == 0)
        return TS_OK;
        
    size_t free_tail = buffer->capacity - buffer->write_pos;
    if (free_tail < length) {
        ts_byte_buffer_compact(buffer);
        free_tail = buffer->capacity - buffer->write_pos;

        if (free_tail < length) {
            size_t need = buffer->write_pos + length;
            if (need > buffer->max_capacity)
                return TS_ERR_NOMEM;
            size_t new_cap = buffer->capacity * 2;
            while (new_cap < need)
            {
                new_cap *= 2;
            }
            
            if (new_cap > buffer->max_capacity)
                new_cap = buffer->max_capacity;

            uint8_t *p = relloc(buffer->data, new_cap);
            if(p == NULL)
                return TS_ERR_NOMEM;

            buffer->data = p;
            buffer->capacity = new_cap;
        }
    }

    memcpy(buffer->data + buffer->write_pos, data, length);
    buffer->write_pos += length;
    return TS_OK;
}

size_t ts_buffer_readable(const ts_byte_buffer_t *buffer) {
    if (buffer == NULL)
        return 0;
    return buffer->write_pos - buffer->read_pos;
}

const uint8_t *ts_buffer_read_ptr(const ts_byte_buffer_t *buffer) {
    if (buffer == NULL || buffer->data == NULL)
        return NULL;
    return 
    buffer->data + buffer->read_pos;
}

void ts_buffer_consume(ts_byte_buffer_t *buffer, size_t length) {
    if (buffer == NULL)
        return;

    size_t readable = buffer->write_pos - buffer->read_pos;
    if (length > readable)
        length = readable;

    buffer->read_pos += length;

    if (buffer->read_pos == buffer->write_pos) {
        buffer->read_pos = 0;
        buffer->write_pos = 0;
    }
}

void ts_byte_buffer_compact(ts_byte_buffer_t *buffer) {
    if (buffer == NULL || buffer->data == NULL)
        return;

    size_t readable = buffer->write_pos - buffer->read_pos;

    if (readable == 0) {
        buffer->read_pos = 0;
        buffer->write_pos = 0;
        return;
    }

    if (buffer->read_pos == 0)
        return;

    memmove(buffer->data, buffer->data + buffer->read_pos, readable);
    buffer->read_pos = 0;
    buffer->write_pos = readable;
}