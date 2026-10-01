#ifndef TRAFFICMON_IPC_PROTOCOL_H
#define TRAFFICMON_IPC_PROTOCOL_H

#include "query.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TM_IPC_PROTOCOL_VERSION = 1,
    TM_IPC_HEADER_SIZE = 20,
    TM_IPC_MAX_REQUEST_SIZE = 64 * 1024,
    TM_IPC_MAX_RESPONSE_SIZE = 1024 * 1024,
    TM_IPC_RESPONSE_BIT = 0x8000,
    TM_IPC_COMMAND_STATUS = 1,
    TM_IPC_COMMAND_LIST_PERIODS = 2,
    TM_IPC_COMMAND_QUERY = 3,
    TM_IPC_COMMAND_COMMIT = 4,
    TM_IPC_COMMAND_PING = 5,
    TM_IPC_COMMAND_DEVICE_JSON = 6,
    TM_IPC_COMMAND_DEVICE_SHOW = 7,
    TM_IPC_COMMAND_DEVICE_DETAIL_JSON = 8,
    TM_IPC_COMMAND_TEST_SEED = 0x7f00,
    TM_IPC_COMMAND_TEST_DATE = 0x7f01,
    TM_IPC_COMMAND_TEST_STOP = 0x7f02,
    TM_IPC_QUERY_REQUEST_SIZE = 20,
    TM_IPC_QUERY_RESPONSE_HEADER_SIZE = 16,
    TM_IPC_QUERY_ROW_SIZE = 104,
    TM_IPC_MAX_QUERY_LIMIT = 10000,
};

typedef enum {
    TM_IPC_STATUS_OK = 0,
    TM_IPC_STATUS_BAD_REQUEST = 1,
    TM_IPC_STATUS_UNSUPPORTED_VERSION = 2,
    TM_IPC_STATUS_UNKNOWN_COMMAND = 3,
    TM_IPC_STATUS_TOO_LARGE = 4,
    TM_IPC_STATUS_DATABASE_ERROR = 5,
    TM_IPC_STATUS_QUERY_ERROR = 6,
    TM_IPC_STATUS_INTERNAL_ERROR = 7,
} tm_ipc_status;

typedef struct {
    uint16_t version;
    uint16_t command;
    uint32_t flags;
    uint32_t payload_length;
    uint32_t request_id;
} tm_ipc_header;

typedef enum {
    TM_IPC_DECODE_OK = 0,
    TM_IPC_DECODE_INVALID,
    TM_IPC_DECODE_UNSUPPORTED_VERSION,
    TM_IPC_DECODE_TOO_LARGE,
    TM_IPC_DECODE_NO_MEMORY,
    TM_IPC_DECODE_CALLBACK_ERROR,
    TM_IPC_DECODE_TRUNCATED,
} tm_ipc_decode_result;

typedef bool (*tm_ipc_frame_callback)(const tm_ipc_header *header, const uint8_t *payload,
                                      void *context);

typedef struct {
    uint8_t *buffer;
    size_t used;
    size_t capacity;
    size_t max_payload;
} tm_ipc_decoder;

typedef struct {
    uint32_t period_start; /* YYYYMMDD, or zero for current. */
    tm_query_spec query;
} tm_ipc_query_request;

typedef struct {
    tm_query_row row;
    char protocol_name[32];
} tm_ipc_query_row;

typedef struct {
    uint32_t period_start;
    uint32_t period_end;
    tm_ipc_query_row *rows;
    size_t count;
} tm_ipc_query_response;

void tm_ipc_header_encode(const tm_ipc_header *header, uint8_t output[TM_IPC_HEADER_SIZE]);
tm_ipc_decode_result tm_ipc_header_decode(const uint8_t input[TM_IPC_HEADER_SIZE],
                                          size_t max_payload, tm_ipc_header *header);
bool tm_ipc_decoder_init(tm_ipc_decoder *decoder, size_t max_payload);
void tm_ipc_decoder_destroy(tm_ipc_decoder *decoder);
tm_ipc_decode_result tm_ipc_decoder_feed(tm_ipc_decoder *decoder, const uint8_t *data,
                                         size_t length, tm_ipc_frame_callback callback,
                                         void *context);
tm_ipc_decode_result tm_ipc_decoder_finish(const tm_ipc_decoder *decoder);
bool tm_ipc_query_request_encode(const tm_ipc_query_request *request,
                                 uint8_t output[TM_IPC_QUERY_REQUEST_SIZE]);
bool tm_ipc_query_request_decode(const uint8_t *data, size_t length,
                                 tm_ipc_query_request *request);
bool tm_ipc_query_response_encode(uint32_t period_start, uint32_t period_end,
                                  const tm_query_result *query, const char *const *protocol_names,
                                  size_t protocol_name_count, uint8_t **payload, size_t *length);
bool tm_ipc_query_response_decode(const uint8_t *payload, size_t length,
                                  tm_ipc_query_response *response);
void tm_ipc_query_response_destroy(tm_ipc_query_response *response);

#endif
