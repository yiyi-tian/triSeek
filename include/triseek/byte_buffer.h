#ifndef TRISEEK_BYTE_BUFFER_H
#define TRISEEK_BYTE_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t max_capacity;
} ts_byte_buffer_t;

int ts_buffer_init(ts_byte_buffer_t *buffer, size_t initial_capacity, size_t max_capacity);
void ts_buffer_destroy(ts_byte_buffer_t *buffer);
int ts_buffer_append(ts_byte_buffer_t *buffer, const void *data, size_t length);
size_t ts_buffer_readable(const ts_byte_buffer_t *buffer);
const uint8_t *ts_buffer_read_ptr(const ts_byte_buffer_t *buffer);
void ts_buffer_consume(ts_byte_buffer_t *buffer, size_t length);
void ts_byte_buffer_compact(ts_byte_buffer_t *buffer);

#endif