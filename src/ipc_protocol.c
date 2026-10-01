#include "ipc_protocol.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t TM_IPC_MAGIC[4] = {'T', 'M', 'I', 'P'};

static void tm_put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void tm_put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void tm_put_u64(uint8_t *output, uint64_t value)
{
    for (size_t index = 0; index < 8; index++) {
        output[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint16_t tm_get_u16(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static uint32_t tm_get_u32(const uint8_t *input)
{
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) | ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static uint64_t tm_get_u64(const uint8_t *input)
{
    uint64_t value = 0;
    for (size_t index = 0; index < 8; index++) {
        value |= (uint64_t)input[index] << (index * 8);
    }
    return value;
}

static size_t tm_bounded_string_length(const char *text, size_t maximum)
{
    size_t length = 0;
    while (length < maximum && text[length] != '\0') {
        length++;
    }
    return length;
}

void tm_ipc_header_encode(const tm_ipc_header *header, uint8_t output[TM_IPC_HEADER_SIZE])
{
    memset(output, 0, TM_IPC_HEADER_SIZE);
    memcpy(output, TM_IPC_MAGIC, sizeof(TM_IPC_MAGIC));
    tm_put_u16(output + 4, header->version);
    tm_put_u16(output + 6, header->command);
    tm_put_u32(output + 8, header->flags);
    tm_put_u32(output + 12, header->payload_length);
    tm_put_u32(output + 16, header->request_id);
}

tm_ipc_decode_result tm_ipc_header_decode(const uint8_t input[TM_IPC_HEADER_SIZE],
                                          size_t max_payload, tm_ipc_header *header)
{
    if (input == NULL || header == NULL || memcmp(input, TM_IPC_MAGIC, sizeof(TM_IPC_MAGIC)) != 0) {
        return TM_IPC_DECODE_INVALID;
    }
    header->version = tm_get_u16(input + 4);
    header->command = tm_get_u16(input + 6);
    header->flags = tm_get_u32(input + 8);
    header->payload_length = tm_get_u32(input + 12);
    header->request_id = tm_get_u32(input + 16);
    if (header->version != TM_IPC_PROTOCOL_VERSION) {
        return TM_IPC_DECODE_UNSUPPORTED_VERSION;
    }
    if (header->payload_length > max_payload) {
        return TM_IPC_DECODE_TOO_LARGE;
    }
    return TM_IPC_DECODE_OK;
}

bool tm_ipc_decoder_init(tm_ipc_decoder *decoder, size_t max_payload)
{
    if (decoder == NULL || max_payload == 0 || max_payload > TM_IPC_MAX_RESPONSE_SIZE) {
        return false;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->capacity = TM_IPC_HEADER_SIZE + max_payload;
    decoder->buffer = malloc(decoder->capacity);
    if (decoder->buffer == NULL) {
        return false;
    }
    decoder->max_payload = max_payload;
    return true;
}

void tm_ipc_decoder_destroy(tm_ipc_decoder *decoder)
{
    if (decoder != NULL) {
        free(decoder->buffer);
        memset(decoder, 0, sizeof(*decoder));
    }
}

tm_ipc_decode_result tm_ipc_decoder_feed(tm_ipc_decoder *decoder, const uint8_t *data,
                                         size_t length, tm_ipc_frame_callback callback,
                                         void *context)
{
    if (decoder == NULL || decoder->buffer == NULL || callback == NULL ||
        (length != 0 && data == NULL)) {
        return TM_IPC_DECODE_INVALID;
    }
    while (length != 0) {
        size_t wanted = TM_IPC_HEADER_SIZE;
        size_t amount;
        tm_ipc_header header;
        tm_ipc_decode_result result;
        if (decoder->used >= TM_IPC_HEADER_SIZE) {
            result = tm_ipc_header_decode(decoder->buffer, decoder->max_payload, &header);
            if (result != TM_IPC_DECODE_OK) {
                return result;
            }
            wanted += header.payload_length;
        }
        amount = wanted - decoder->used;
        if (amount > length) {
            amount = length;
        }
        memcpy(decoder->buffer + decoder->used, data, amount);
        decoder->used += amount;
        data += amount;
        length -= amount;

        if (decoder->used < TM_IPC_HEADER_SIZE) {
            continue;
        }
        result = tm_ipc_header_decode(decoder->buffer, decoder->max_payload, &header);
        if (result != TM_IPC_DECODE_OK) {
            return result;
        }
        wanted = TM_IPC_HEADER_SIZE + header.payload_length;
        if (decoder->used == wanted) {
            if (!callback(&header, decoder->buffer + TM_IPC_HEADER_SIZE, context)) {
                return TM_IPC_DECODE_CALLBACK_ERROR;
            }
            decoder->used = 0;
        }
    }
    return TM_IPC_DECODE_OK;
}

tm_ipc_decode_result tm_ipc_decoder_finish(const tm_ipc_decoder *decoder)
{
    if (decoder == NULL || decoder->buffer == NULL) {
        return TM_IPC_DECODE_INVALID;
    }
    return decoder->used == 0 ? TM_IPC_DECODE_OK : TM_IPC_DECODE_TRUNCATED;
}

bool tm_ipc_query_request_encode(const tm_ipc_query_request *request,
                                 uint8_t output[TM_IPC_QUERY_REQUEST_SIZE])
{
    if (request == NULL || output == NULL || request->query.order > TM_ORDER_CONNECTIONS ||
        request->query.limit > TM_IPC_MAX_QUERY_LIMIT || request->query.offset > UINT32_MAX) {
        return false;
    }
    memset(output, 0, TM_IPC_QUERY_REQUEST_SIZE);
    tm_put_u32(output, request->period_start);
    tm_put_u32(output + 4, request->query.group_by);
    output[8] = (uint8_t)request->query.order;
    output[9] = request->query.descending ? 1 : 0;
    tm_put_u32(output + 12, (uint32_t)request->query.limit);
    tm_put_u32(output + 16, (uint32_t)request->query.offset);
    return true;
}

bool tm_ipc_query_request_decode(const uint8_t *data, size_t length,
                                 tm_ipc_query_request *request)
{
    uint32_t allowed_groups =
        TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC | TM_GROUP_PROTOCOL | TM_GROUP_DEVICE;
    if (data == NULL || request == NULL || length != TM_IPC_QUERY_REQUEST_SIZE || data[9] > 1 ||
        data[10] != 0 || data[11] != 0) {
        return false;
    }
    memset(request, 0, sizeof(*request));
    request->period_start = tm_get_u32(data);
    request->query.group_by = tm_get_u32(data + 4);
    request->query.order = (tm_query_order)data[8];
    request->query.descending = data[9] != 0;
    request->query.limit = tm_get_u32(data + 12);
    request->query.offset = tm_get_u32(data + 16);
    if ((request->query.group_by & ~allowed_groups) != 0 ||
        ((request->query.group_by & TM_GROUP_DEVICE) != 0 &&
         (request->query.group_by & (TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC)) != 0) ||
        request->query.order > TM_ORDER_CONNECTIONS ||
        request->query.limit > TM_IPC_MAX_QUERY_LIMIT) {
        return false;
    }
    if (request->query.limit == 0) {
        request->query.limit = TM_IPC_MAX_QUERY_LIMIT;
    }
    return true;
}

static void tm_encode_query_row(const tm_ipc_query_row *source, uint8_t *output)
{
    memset(output, 0, TM_IPC_QUERY_ROW_SIZE);
    output[0] = source->row.key.family;
    output[1] = source->row.key.mac_known ? 1 : 0;
    tm_put_u16(output + 2, source->row.key.protocol_id);
    memcpy(output + 4, source->row.key.host_ip.bytes, TM_IPV6_LENGTH);
    if (source->row.key.mac_known) {
        memcpy(output + 20, source->row.key.mac, TM_MAC_LENGTH);
    }
    tm_put_u64(output + 28, source->row.connections);
    tm_put_u64(output + 36, source->row.rx_bytes);
    tm_put_u64(output + 44, source->row.rx_packets);
    tm_put_u64(output + 52, source->row.tx_bytes);
    tm_put_u64(output + 60, source->row.tx_packets);
    memcpy(output + 68, source->protocol_name,
           tm_bounded_string_length(source->protocol_name, 31));
}

bool tm_ipc_query_response_encode(uint32_t period_start, uint32_t period_end,
                                  const tm_query_result *query, const char *const *protocol_names,
                                  size_t protocol_name_count, uint8_t **payload, size_t *length)
{
    size_t payload_length;
    uint8_t *output;
    size_t index;
    if (query == NULL || payload == NULL || length == NULL || query->count > TM_IPC_MAX_QUERY_LIMIT ||
        query->count > (TM_IPC_MAX_RESPONSE_SIZE - TM_IPC_QUERY_RESPONSE_HEADER_SIZE) /
                           TM_IPC_QUERY_ROW_SIZE) {
        return false;
    }
    payload_length = TM_IPC_QUERY_RESPONSE_HEADER_SIZE + query->count * TM_IPC_QUERY_ROW_SIZE;
    output = calloc(1, payload_length);
    if (output == NULL) {
        return false;
    }
    tm_put_u32(output, period_start);
    tm_put_u32(output + 4, period_end);
    tm_put_u32(output + 8, (uint32_t)query->count);
    for (index = 0; index < query->count; index++) {
        tm_ipc_query_row row;
        uint16_t protocol_id = query->rows[index].key.protocol_id;
        memset(&row, 0, sizeof(row));
        row.row = query->rows[index];
        if (protocol_names != NULL && protocol_id < protocol_name_count &&
            protocol_names[protocol_id] != NULL) {
            size_t name_length = strlen(protocol_names[protocol_id]);
            if (name_length > sizeof(row.protocol_name) - 1) {
                name_length = sizeof(row.protocol_name) - 1;
            }
            memcpy(row.protocol_name, protocol_names[protocol_id], name_length);
        } else {
            memcpy(row.protocol_name, "Other", sizeof("Other"));
        }
        tm_encode_query_row(&row,
                            output + TM_IPC_QUERY_RESPONSE_HEADER_SIZE +
                                index * TM_IPC_QUERY_ROW_SIZE);
    }
    *payload = output;
    *length = payload_length;
    return true;
}

bool tm_ipc_query_response_decode(const uint8_t *payload, size_t length,
                                  tm_ipc_query_response *response)
{
    uint32_t count;
    size_t expected_length;
    if (payload == NULL || response == NULL || length < TM_IPC_QUERY_RESPONSE_HEADER_SIZE) {
        return false;
    }
    memset(response, 0, sizeof(*response));
    count = tm_get_u32(payload + 8);
    if (tm_get_u32(payload + 12) != 0 || count > TM_IPC_MAX_QUERY_LIMIT) {
        return false;
    }
    expected_length = TM_IPC_QUERY_RESPONSE_HEADER_SIZE + (size_t)count * TM_IPC_QUERY_ROW_SIZE;
    if (length != expected_length) {
        return false;
    }
    if (count != 0) {
        response->rows = calloc(count, sizeof(*response->rows));
        if (response->rows == NULL) {
            return false;
        }
    }
    response->period_start = tm_get_u32(payload);
    response->period_end = tm_get_u32(payload + 4);
    if (response->period_start == 0 || response->period_end == 0 ||
        response->period_start >= response->period_end) {
        tm_ipc_query_response_destroy(response);
        return false;
    }
    response->count = count;
    for (size_t index = 0; index < count; index++) {
        const uint8_t *input = payload + TM_IPC_QUERY_RESPONSE_HEADER_SIZE +
                               index * TM_IPC_QUERY_ROW_SIZE;
        tm_ipc_query_row *row = &response->rows[index];
        size_t name_length;
        row->row.key.family = input[0];
        row->row.key.mac_known = input[1] != 0;
        row->row.key.protocol_id = tm_get_u16(input + 2);
        if ((row->row.key.family != 0 && !tm_family_is_valid(row->row.key.family)) || input[1] > 1 ||
            tm_get_u16(input + 2) >= 256) {
            tm_ipc_query_response_destroy(response);
            return false;
        }
        memcpy(row->row.key.host_ip.bytes, input + 4, TM_IPV6_LENGTH);
        memcpy(row->row.key.mac, input + 20, TM_MAC_LENGTH);
        if (!row->row.key.mac_known) {
            for (size_t mac_index = 20; mac_index < 26; mac_index++) {
                if (input[mac_index] != 0) {
                    tm_ipc_query_response_destroy(response);
                    return false;
                }
            }
            memset(row->row.key.mac, 0, TM_MAC_LENGTH);
        }
        if ((input[26] != 0 || input[27] != 0) ||
            (row->row.key.family == TM_FAMILY_IPV4 &&
             memcmp(input + 8, "\0\0\0\0\0\0\0\0\0\0\0\0", 12) != 0) ||
            (row->row.key.family == 0 &&
             memcmp(input + 4, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0)) {
            tm_ipc_query_response_destroy(response);
            return false;
        }
        row->row.connections = tm_get_u64(input + 28);
        row->row.rx_bytes = tm_get_u64(input + 36);
        row->row.rx_packets = tm_get_u64(input + 44);
        row->row.tx_bytes = tm_get_u64(input + 52);
        row->row.tx_packets = tm_get_u64(input + 60);
        name_length = tm_bounded_string_length((const char *)(input + 68), 32);
        if (name_length == 32) {
            tm_ipc_query_response_destroy(response);
            return false;
        }
        memcpy(row->protocol_name, input + 68, name_length);
        row->protocol_name[name_length] = '\0';
        for (size_t reserved = 100; reserved < TM_IPC_QUERY_ROW_SIZE; reserved++) {
            if (input[reserved] != 0) {
                tm_ipc_query_response_destroy(response);
                return false;
            }
        }
    }
    return true;
}

void tm_ipc_query_response_destroy(tm_ipc_query_response *response)
{
    if (response != NULL) {
        free(response->rows);
        memset(response, 0, sizeof(*response));
    }
}
