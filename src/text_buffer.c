#include "text_buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TM_TEXT_BUFFER_INITIAL = 256 };

bool tm_text_buffer_init(tm_text_buffer *buffer)
{
    if (buffer == NULL) {
        return false;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->data = malloc(TM_TEXT_BUFFER_INITIAL);
    if (buffer->data == NULL) {
        return false;
    }
    buffer->capacity = TM_TEXT_BUFFER_INITIAL;
    buffer->data[0] = '\0';
    return true;
}

void tm_text_buffer_destroy(tm_text_buffer *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
    }
}

static bool tm_text_buffer_reserve(tm_text_buffer *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    char *resized;
    if (buffer == NULL || buffer->data == NULL || additional > TM_TEXT_BUFFER_MAX - 1 ||
        buffer->length > TM_TEXT_BUFFER_MAX - 1 - additional) {
        return false;
    }
    required = buffer->length + additional + 1;
    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity;
    while (capacity < required) {
        if (capacity > TM_TEXT_BUFFER_MAX / 2) {
            capacity = TM_TEXT_BUFFER_MAX;
            break;
        }
        capacity *= 2;
    }
    if (capacity < required) {
        return false;
    }
    resized = realloc(buffer->data, capacity);
    if (resized == NULL) {
        return false;
    }
    buffer->data = resized;
    buffer->capacity = capacity;
    return true;
}

bool tm_text_buffer_append_n(tm_text_buffer *buffer, const char *text, size_t length)
{
    if (text == NULL || !tm_text_buffer_reserve(buffer, length)) {
        return false;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

bool tm_text_buffer_append(tm_text_buffer *buffer, const char *text)
{
    return text != NULL && tm_text_buffer_append_n(buffer, text, strlen(text));
}

bool tm_text_buffer_printf(tm_text_buffer *buffer, const char *format, ...)
{
    va_list arguments;
    va_list copy;
    int required;
    if (buffer == NULL || format == NULL) {
        return false;
    }
    va_start(arguments, format);
    va_copy(copy, arguments);
    required = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (required < 0 || !tm_text_buffer_reserve(buffer, (size_t)required)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format,
                    arguments);
    va_end(arguments);
    buffer->length += (size_t)required;
    return true;
}
