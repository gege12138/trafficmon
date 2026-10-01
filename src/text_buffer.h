#ifndef TRAFFICMON_TEXT_BUFFER_H
#define TRAFFICMON_TEXT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

enum { TM_TEXT_BUFFER_MAX = 8 * 1024 * 1024 };

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} tm_text_buffer;

bool tm_text_buffer_init(tm_text_buffer *buffer);
void tm_text_buffer_destroy(tm_text_buffer *buffer);
bool tm_text_buffer_append(tm_text_buffer *buffer, const char *text);
bool tm_text_buffer_append_n(tm_text_buffer *buffer, const char *text, size_t length);
bool tm_text_buffer_printf(tm_text_buffer *buffer, const char *format, ...);

#endif
