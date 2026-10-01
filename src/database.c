#include "database.h"

#include "subnet.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t TM_DATABASE_MAGIC[8] = {'T', 'R', 'A', 'F', 'D', 'B', '0', '2'};

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
    size_t index;
    for (index = 0; index < 8; index++) {
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
    size_t index;
    for (index = 0; index < 8; index++) {
        value |= (uint64_t)input[index] << (index * 8);
    }
    return value;
}

static uint32_t tm_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    for (index = 0; index < length; index++) {
        unsigned int bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static int tm_entry_compare(const tm_aggregate_entry *left, const tm_aggregate_entry *right)
{
    int comparison;
    size_t ip_length;
    if (left->key.family != right->key.family) {
        return left->key.family < right->key.family ? -1 : 1;
    }
    ip_length = tm_ip_size(left->key.family);
    comparison = memcmp(left->key.host_ip.bytes, right->key.host_ip.bytes, ip_length);
    if (comparison != 0) {
        return comparison;
    }
    if (left->key.mac_known != right->key.mac_known) {
        return left->key.mac_known ? 1 : -1;
    }
    comparison = memcmp(left->key.mac, right->key.mac, TM_MAC_LENGTH);
    if (comparison != 0) {
        return comparison;
    }
    if (left->key.protocol_id != right->key.protocol_id) {
        return left->key.protocol_id < right->key.protocol_id ? -1 : 1;
    }
    return 0;
}

static int tm_entry_qsort_compare(const void *left, const void *right)
{
    return tm_entry_compare(left, right);
}

static bool tm_metadata_valid(const tm_database_metadata *metadata)
{
    return metadata != NULL && metadata->flags == 0 && metadata->protocol_fingerprint != 0 &&
           tm_date_is_valid(metadata->period.start) && tm_date_is_valid(metadata->period.end) &&
           tm_date_encode(metadata->period.start) < tm_date_encode(metadata->period.end);
}

tm_database_result tm_database_encode(const tm_aggregator *aggregator,
                                      const tm_database_metadata *metadata,
                                      tm_database_buffer *buffer)
{
    tm_aggregate_entry *sorted = NULL;
    size_t length;
    size_t source_index;
    size_t output_index = 0;
    uint8_t *output;

    if (aggregator == NULL || buffer == NULL || !tm_metadata_valid(metadata) ||
        aggregator->count > UINT32_MAX || aggregator->count > TM_DATABASE_HARD_RECORD_LIMIT) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    memset(buffer, 0, sizeof(*buffer));
    if (aggregator->count > (SIZE_MAX - TM_DATABASE_HEADER_SIZE) / TM_DATABASE_RECORD_SIZE) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    length = TM_DATABASE_HEADER_SIZE + aggregator->count * TM_DATABASE_RECORD_SIZE;
    output = calloc(1, length);
    if (output == NULL) {
        return TM_DATABASE_NO_MEMORY;
    }
    /* calloc keeps GCC -fanalyzer sound when it explores contradictory count branches. */
    sorted = calloc(aggregator->count == 0 ? 1 : aggregator->count, sizeof(*sorted));
    if (sorted == NULL) {
        free(output);
        return TM_DATABASE_NO_MEMORY;
    }
    if (aggregator->count != 0) {
        for (source_index = 0; source_index < aggregator->capacity; source_index++) {
            if (aggregator->entries[source_index].occupied) {
                sorted[output_index++] = aggregator->entries[source_index];
            }
        }
        if (output_index != aggregator->count) {
            free(sorted);
            free(output);
            return TM_DATABASE_INVALID_ARGUMENT;
        }
        qsort(sorted, aggregator->count, sizeof(*sorted), tm_entry_qsort_compare);
    }

    memcpy(output, TM_DATABASE_MAGIC, sizeof(TM_DATABASE_MAGIC));
    tm_put_u16(output + 8, TM_DATABASE_FORMAT_VERSION);
    tm_put_u16(output + 10, TM_DATABASE_HEADER_SIZE);
    tm_put_u16(output + 12, TM_DATABASE_RECORD_SIZE);
    tm_put_u32(output + 16, (uint32_t)aggregator->count);
    tm_put_u32(output + 20, tm_date_encode(metadata->period.start));
    tm_put_u32(output + 24, tm_date_encode(metadata->period.end));
    tm_put_u32(output + 28, metadata->flags);
    tm_put_u64(output + 40, metadata->protocol_fingerprint);

    for (output_index = 0; output_index < aggregator->count; output_index++) {
        const tm_aggregate_entry *entry = &sorted[output_index];
        uint8_t *record = output + TM_DATABASE_HEADER_SIZE + output_index * TM_DATABASE_RECORD_SIZE;
        record[0] = entry->key.family;
        record[1] = entry->key.mac_known ? 1 : 0;
        tm_put_u16(record + 2, entry->key.protocol_id);
        memcpy(record + 4, entry->key.host_ip.bytes, TM_IPV6_LENGTH);
        if (entry->key.mac_known) {
            memcpy(record + 20, entry->key.mac, TM_MAC_LENGTH);
        }
        tm_put_u64(record + 32, entry->connections);
        tm_put_u64(record + 40, entry->rx_bytes);
        tm_put_u64(record + 48, entry->rx_packets);
        tm_put_u64(record + 56, entry->tx_bytes);
        tm_put_u64(record + 64, entry->tx_packets);
    }
    free(sorted);
    tm_put_u32(output + 32,
               tm_crc32(output + TM_DATABASE_HEADER_SIZE, length - TM_DATABASE_HEADER_SIZE));
    tm_put_u32(output + 36, 0);
    tm_put_u32(output + 36, tm_crc32(output, TM_DATABASE_HEADER_SIZE));
    buffer->data = output;
    buffer->length = length;
    return TM_DATABASE_OK;
}

void tm_database_buffer_destroy(tm_database_buffer *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
    }
}

static tm_database_result tm_decode_header(const uint8_t *data, size_t length, size_t record_limit,
                                           tm_database_metadata *metadata)
{
    uint8_t header[TM_DATABASE_HEADER_SIZE];
    uint32_t count;
    uint32_t expected_crc;
    size_t expected_length;
    if (data == NULL || length < TM_DATABASE_HEADER_SIZE) {
        return TM_DATABASE_INVALID_LENGTH;
    }
    if (memcmp(data, TM_DATABASE_MAGIC, sizeof(TM_DATABASE_MAGIC)) != 0) {
        return TM_DATABASE_BAD_MAGIC;
    }
    if (tm_get_u16(data + 8) != TM_DATABASE_FORMAT_VERSION) {
        return TM_DATABASE_UNSUPPORTED_VERSION;
    }
    if (tm_get_u16(data + 10) != TM_DATABASE_HEADER_SIZE ||
        tm_get_u16(data + 12) != TM_DATABASE_RECORD_SIZE || tm_get_u16(data + 14) != 0) {
        return TM_DATABASE_INVALID_HEADER;
    }
    memcpy(header, data, sizeof(header));
    expected_crc = tm_get_u32(header + 36);
    tm_put_u32(header + 36, 0);
    if (tm_crc32(header, sizeof(header)) != expected_crc) {
        return TM_DATABASE_CHECKSUM_MISMATCH;
    }
    count = tm_get_u32(data + 16);
    if (count > TM_DATABASE_HARD_RECORD_LIMIT || (record_limit != 0 && count > record_limit)) {
        return TM_DATABASE_LIMIT_EXCEEDED;
    }
    expected_length = TM_DATABASE_HEADER_SIZE + (size_t)count * TM_DATABASE_RECORD_SIZE;
    if (length != expected_length) {
        return TM_DATABASE_INVALID_LENGTH;
    }
    if (tm_crc32(data + TM_DATABASE_HEADER_SIZE, length - TM_DATABASE_HEADER_SIZE) !=
        tm_get_u32(data + 32)) {
        return TM_DATABASE_CHECKSUM_MISMATCH;
    }
    memset(metadata, 0, sizeof(*metadata));
    metadata->record_count = count;
    metadata->flags = tm_get_u32(data + 28);
    metadata->protocol_fingerprint = tm_get_u64(data + 40);
    if (metadata->flags != 0 || metadata->protocol_fingerprint == 0 ||
        !tm_date_decode(tm_get_u32(data + 20), &metadata->period.start) ||
        !tm_date_decode(tm_get_u32(data + 24), &metadata->period.end) ||
        tm_date_encode(metadata->period.start) >= tm_date_encode(metadata->period.end)) {
        return TM_DATABASE_INVALID_HEADER;
    }
    for (size_t index = 48; index < TM_DATABASE_HEADER_SIZE; index++) {
        if (data[index] != 0) {
            return TM_DATABASE_INVALID_HEADER;
        }
    }
    return TM_DATABASE_OK;
}

static tm_database_result tm_decode_record(const uint8_t *record, tm_aggregate_entry *entry)
{
    size_t index;
    memset(entry, 0, sizeof(*entry));
    entry->key.family = record[0];
    if (!tm_family_is_valid(entry->key.family) || record[1] > 1 || tm_get_u16(record + 2) >= 256) {
        return TM_DATABASE_INVALID_RECORD;
    }
    entry->key.mac_known = record[1] != 0;
    entry->key.protocol_id = tm_get_u16(record + 2);
    memcpy(entry->key.host_ip.bytes, record + 4, TM_IPV6_LENGTH);
    memcpy(entry->key.mac, record + 20, TM_MAC_LENGTH);
    if (entry->key.family == TM_FAMILY_IPV4) {
        for (index = 8; index < 20; index++) {
            if (record[index] != 0) {
                return TM_DATABASE_INVALID_RECORD;
            }
        }
    }
    if (!entry->key.mac_known) {
        for (index = 20; index < 26; index++) {
            if (record[index] != 0) {
                return TM_DATABASE_INVALID_RECORD;
            }
        }
    }
    for (index = 26; index < 32; index++) {
        if (record[index] != 0) {
            return TM_DATABASE_INVALID_RECORD;
        }
    }
    entry->connections = tm_get_u64(record + 32);
    entry->rx_bytes = tm_get_u64(record + 40);
    entry->rx_packets = tm_get_u64(record + 48);
    entry->tx_bytes = tm_get_u64(record + 56);
    entry->tx_packets = tm_get_u64(record + 64);
    entry->occupied = true;
    return TM_DATABASE_OK;
}

tm_database_result tm_database_validate(const uint8_t *data, size_t length, size_t record_limit,
                                        tm_database_metadata *metadata)
{
    tm_database_metadata decoded;
    tm_database_result result = tm_decode_header(data, length, record_limit, &decoded);
    size_t index;
    if (result != TM_DATABASE_OK) {
        return result;
    }
    for (index = 0; index < decoded.record_count; index++) {
        tm_aggregate_entry entry;
        result = tm_decode_record(data + TM_DATABASE_HEADER_SIZE + index * TM_DATABASE_RECORD_SIZE,
                                  &entry);
        if (result != TM_DATABASE_OK) {
            return result;
        }
    }
    if (metadata != NULL) {
        *metadata = decoded;
    }
    return TM_DATABASE_OK;
}

tm_database_result tm_database_decode(const uint8_t *data, size_t length, size_t record_limit,
                                      tm_aggregator *aggregator, tm_database_metadata *metadata)
{
    tm_database_metadata decoded;
    tm_aggregator restored;
    tm_database_result result;
    size_t index;
    if (aggregator == NULL || metadata == NULL) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    memset(aggregator, 0, sizeof(*aggregator));
    memset(metadata, 0, sizeof(*metadata));
    result = tm_decode_header(data, length, record_limit, &decoded);
    if (result != TM_DATABASE_OK) {
        return result;
    }
    if (!tm_aggregator_init(&restored, record_limit)) {
        return TM_DATABASE_NO_MEMORY;
    }
    for (index = 0; index < decoded.record_count; index++) {
        tm_aggregate_entry entry;
        result = tm_decode_record(data + TM_DATABASE_HEADER_SIZE + index * TM_DATABASE_RECORD_SIZE,
                                  &entry);
        if (result != TM_DATABASE_OK || !tm_aggregator_restore_entry(&restored, &entry)) {
            tm_aggregator_destroy(&restored);
            return result == TM_DATABASE_OK ? TM_DATABASE_INVALID_RECORD : result;
        }
    }
    *aggregator = restored;
    *metadata = decoded;
    return TM_DATABASE_OK;
}

const char *tm_database_result_string(tm_database_result result)
{
    static const char *const messages[] = {
        "ok",          "invalid argument", "out of memory",       "bad magic",
        "unsupported version", "invalid header",  "invalid record",      "invalid length",
        "record limit exceeded", "checksum mismatch", "protocol database mismatch",
    };
    if ((size_t)result >= sizeof(messages) / sizeof(messages[0])) {
        return "unknown database error";
    }
    return messages[result];
}
