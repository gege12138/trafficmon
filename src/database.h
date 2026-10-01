#ifndef TRAFFICMON_DATABASE_H
#define TRAFFICMON_DATABASE_H

#include "aggregator.h"
#include "period.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TM_DATABASE_FORMAT_VERSION = 2,
    TM_DATABASE_HEADER_SIZE = 64,
    TM_DATABASE_RECORD_SIZE = 72,
    TM_DATABASE_HARD_RECORD_LIMIT = 1000000,
};

typedef struct {
    tm_period period;
    uint32_t flags;
    uint32_t record_count;
    uint64_t protocol_fingerprint;
} tm_database_metadata;

typedef struct {
    uint8_t *data;
    size_t length;
} tm_database_buffer;

typedef enum {
    TM_DATABASE_OK = 0,
    TM_DATABASE_INVALID_ARGUMENT,
    TM_DATABASE_NO_MEMORY,
    TM_DATABASE_BAD_MAGIC,
    TM_DATABASE_UNSUPPORTED_VERSION,
    TM_DATABASE_INVALID_HEADER,
    TM_DATABASE_INVALID_RECORD,
    TM_DATABASE_INVALID_LENGTH,
    TM_DATABASE_LIMIT_EXCEEDED,
    TM_DATABASE_CHECKSUM_MISMATCH,
    TM_DATABASE_PROTOCOL_MISMATCH,
} tm_database_result;

/* On success, buffer owns heap memory released with tm_database_buffer_destroy(). */
tm_database_result tm_database_encode(const tm_aggregator *aggregator,
                                      const tm_database_metadata *metadata,
                                      tm_database_buffer *buffer);
void tm_database_buffer_destroy(tm_database_buffer *buffer);

/* Decoder is independent of filesystem/global state and is suitable for fuzzing. */
tm_database_result tm_database_decode(const uint8_t *data, size_t length, size_t record_limit,
                                      tm_aggregator *aggregator,
                                      tm_database_metadata *metadata);
tm_database_result tm_database_validate(const uint8_t *data, size_t length, size_t record_limit,
                                        tm_database_metadata *metadata);
const char *tm_database_result_string(tm_database_result result);

#endif
