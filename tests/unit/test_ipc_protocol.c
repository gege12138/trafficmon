#include "ipc_protocol.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t frames;
    uint32_t request_ids[4];
    size_t payload_lengths[4];
} ipc_capture;

static bool capture_frame(const tm_ipc_header *header, const uint8_t *payload, void *context)
{
    ipc_capture *capture = context;
    if (capture->frames >= 4) {
        return false;
    }
    capture->request_ids[capture->frames] = header->request_id;
    capture->payload_lengths[capture->frames] = header->payload_length;
    if (header->payload_length != 0) {
        TM_EXPECT_TRUE(payload[0] == (uint8_t)header->request_id);
    }
    capture->frames++;
    return true;
}

static size_t append_frame(uint8_t *output, uint16_t command, uint32_t request_id,
                           const uint8_t *payload, size_t payload_length)
{
    tm_ipc_header header = {
        .version = TM_IPC_PROTOCOL_VERSION,
        .command = command,
        .payload_length = (uint32_t)payload_length,
        .request_id = request_id,
    };
    tm_ipc_header_encode(&header, output);
    if (payload_length != 0) {
        memcpy(output + TM_IPC_HEADER_SIZE, payload, payload_length);
    }
    return TM_IPC_HEADER_SIZE + payload_length;
}

void test_ipc_protocol(void)
{
    uint8_t stream[128];
    uint8_t first_payload[] = {1, 2, 3};
    uint8_t second_payload[] = {2};
    size_t first_length = append_frame(stream, TM_IPC_COMMAND_QUERY, 1, first_payload,
                                       sizeof(first_payload));
    size_t second_length = append_frame(stream + first_length, TM_IPC_COMMAND_COMMIT, 2,
                                        second_payload, sizeof(second_payload));
    size_t third_length = append_frame(stream + first_length + second_length, TM_IPC_COMMAND_PING,
                                       3, NULL, 0);
    size_t total_length = first_length + second_length + third_length;
    tm_ipc_decoder decoder;
    ipc_capture capture;
    tm_ipc_header decoded;

    memset(&capture, 0, sizeof(capture));
    TM_EXPECT_TRUE(tm_ipc_decoder_init(&decoder, TM_IPC_MAX_REQUEST_SIZE));
    for (size_t index = 0; index < total_length; index++) {
        TM_EXPECT_EQ_U64(TM_IPC_DECODE_OK,
                         tm_ipc_decoder_feed(&decoder, &stream[index], 1, capture_frame, &capture));
    }
    TM_EXPECT_EQ_U64(3, capture.frames);
    TM_EXPECT_EQ_U64(1, capture.request_ids[0]);
    TM_EXPECT_EQ_U64(2, capture.request_ids[1]);
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_OK, tm_ipc_decoder_finish(&decoder));
    tm_ipc_decoder_destroy(&decoder);

    memset(&capture, 0, sizeof(capture));
    TM_EXPECT_TRUE(tm_ipc_decoder_init(&decoder, TM_IPC_MAX_REQUEST_SIZE));
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_OK,
                     tm_ipc_decoder_feed(&decoder, stream, 7, capture_frame, &capture));
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_OK,
                     tm_ipc_decoder_feed(&decoder, stream + 7, total_length - 7,
                                         capture_frame, &capture));
    TM_EXPECT_EQ_U64(3, capture.frames);
    tm_ipc_decoder_destroy(&decoder);

    stream[0] = 'X';
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_INVALID,
                     tm_ipc_header_decode(stream, TM_IPC_MAX_REQUEST_SIZE, &decoded));
    stream[0] = 'T';
    stream[4] = 99;
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_UNSUPPORTED_VERSION,
                     tm_ipc_header_decode(stream, TM_IPC_MAX_REQUEST_SIZE, &decoded));
    stream[4] = TM_IPC_PROTOCOL_VERSION;
    stream[12] = 0xff;
    stream[13] = 0xff;
    stream[14] = 0xff;
    stream[15] = 0x7f;
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_TOO_LARGE,
                     tm_ipc_header_decode(stream, TM_IPC_MAX_REQUEST_SIZE, &decoded));

    TM_EXPECT_TRUE(tm_ipc_decoder_init(&decoder, TM_IPC_MAX_REQUEST_SIZE));
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_OK,
                     tm_ipc_decoder_feed(&decoder, stream + first_length, 5, capture_frame,
                                         &capture));
    TM_EXPECT_EQ_U64(TM_IPC_DECODE_TRUNCATED, tm_ipc_decoder_finish(&decoder));
    tm_ipc_decoder_destroy(&decoder);

    {
        tm_ipc_query_request request = {
            .period_start = 20260901,
            .query = {TM_GROUP_FAMILY | TM_GROUP_IP, TM_ORDER_RX_BYTES, true, 25, 3},
        };
        tm_ipc_query_request decoded_request;
        uint8_t request_bytes[TM_IPC_QUERY_REQUEST_SIZE];
        TM_EXPECT_TRUE(tm_ipc_query_request_encode(&request, request_bytes));
        TM_EXPECT_TRUE(
            tm_ipc_query_request_decode(request_bytes, sizeof(request_bytes), &decoded_request));
        TM_EXPECT_EQ_U64(request.period_start, decoded_request.period_start);
        TM_EXPECT_EQ_U64(request.query.group_by, decoded_request.query.group_by);
        TM_EXPECT_EQ_U64(request.query.limit, decoded_request.query.limit);

        request.query.limit = TM_IPC_MAX_QUERY_LIMIT + 1U;
        TM_EXPECT_TRUE(!tm_ipc_query_request_encode(&request, request_bytes));
        request.query.limit = 0;
        TM_EXPECT_TRUE(tm_ipc_query_request_encode(&request, request_bytes));
        TM_EXPECT_TRUE(
            tm_ipc_query_request_decode(request_bytes, sizeof(request_bytes), &decoded_request));
        TM_EXPECT_EQ_U64(TM_IPC_MAX_QUERY_LIMIT, decoded_request.query.limit);
    }

    {
        tm_query_result query;
        uint8_t *payload = NULL;
        size_t payload_length = 0;
        memset(&query, 0, sizeof(query));
        query.count = TM_IPC_MAX_QUERY_LIMIT;
        query.rows = calloc(query.count, sizeof(*query.rows));
        TM_EXPECT_TRUE(query.rows != NULL);
        if (query.rows != NULL) {
            TM_EXPECT_TRUE(tm_ipc_query_response_encode(20260901, 20261001, &query, NULL, 0,
                                                        &payload, &payload_length));
            TM_EXPECT_EQ_U64(TM_IPC_QUERY_RESPONSE_HEADER_SIZE +
                                 TM_IPC_MAX_QUERY_LIMIT * TM_IPC_QUERY_ROW_SIZE,
                             payload_length);
            TM_EXPECT_TRUE(payload_length <= TM_IPC_MAX_RESPONSE_SIZE);
        }
        free(payload);
        free(query.rows);
    }
}
