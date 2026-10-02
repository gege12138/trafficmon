#include "journal/journal.h"

#include "subnet.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TM_FILENO _fileno
#define TM_FSYNC _commit
#else
#include <unistd.h>
#define TM_FILENO fileno
#define TM_FSYNC fsync
#endif

enum {
    TM_JOURNAL_VERSION = 1,
    TM_JOURNAL_FLUSH_SECONDS = 5,
    TM_JOURNAL_RECORD_PAYLOAD = 124,
};

static const uint8_t tm_journal_magic[8] = {'T', 'M', 'J', 'R', 'N', 'L', '1', 0};
static const uint8_t tm_record_magic[4] = {'J', 'R', 'E', 'C'};

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
    for (unsigned int index = 0; index < 8; index++) {
        output[index] = (uint8_t)(value >> (index * 8U));
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
    for (unsigned int index = 0; index < 8; index++) {
        value |= (uint64_t)input[index] << (index * 8U);
    }
    return value;
}

static uint32_t tm_crc32(const uint8_t *data, size_t length)
{
    static const uint32_t table[16] = {
        UINT32_C(0x00000000), UINT32_C(0x1db71064), UINT32_C(0x3b6e20c8),
        UINT32_C(0x26d930ac), UINT32_C(0x76dc4190), UINT32_C(0x6b6b51f4),
        UINT32_C(0x4db26158), UINT32_C(0x5005713c), UINT32_C(0xedb88320),
        UINT32_C(0xf00f9344), UINT32_C(0xd6d6a3e8), UINT32_C(0xcb61b38c),
        UINT32_C(0x9b64c2b0), UINT32_C(0x86d3d2d4), UINT32_C(0xa00ae278),
        UINT32_C(0xbdbdf21c),
    };
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        crc = (crc >> 4) ^ table[crc & 0x0fU];
        crc = (crc >> 4) ^ table[crc & 0x0fU];
    }
    return ~crc;
}

static size_t tm_bounded_string_length(const char *value, size_t limit)
{
    size_t length = 0;
    while (length < limit && value[length] != '\0') {
        length++;
    }
    return length;
}

static void tm_journal_error(tm_journal *journal, const char *operation)
{
    if (!journal->error_reported) {
        fprintf(stderr, "trafficmon: journal %s failed: %s\n", operation,
                errno != 0 ? strerror(errno) : "invalid journal data");
        journal->error_reported = true;
    }
}

static bool tm_gmtime(int64_t timestamp, struct tm *result)
{
    time_t value;
    if (timestamp < 0 || result == NULL) {
        return false;
    }
    value = (time_t)timestamp;
    if ((int64_t)value != timestamp) {
        return false;
    }
#ifdef _WIN32
    return gmtime_s(result, &value) == 0;
#else
    return gmtime_r(&value, result) != NULL;
#endif
}

bool tm_journal_shard_path(const char *directory, int64_t timestamp,
                           char output[TM_PATH_MAX])
{
    struct tm utc;
    int written;
    if (directory == NULL || directory[0] == '\0' || output == NULL ||
        !tm_gmtime(timestamp, &utc)) {
        return false;
    }
    written = snprintf(output, TM_PATH_MAX, "%s/journal-%04d%02d%02d.bin", directory,
                       utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    return written > 0 && written < TM_PATH_MAX;
}

static bool tm_header_encode(uint8_t output[TM_JOURNAL_HEADER_SIZE])
{
    memset(output, 0, TM_JOURNAL_HEADER_SIZE);
    memcpy(output, tm_journal_magic, sizeof(tm_journal_magic));
    tm_put_u16(output + 8, TM_JOURNAL_VERSION);
    tm_put_u16(output + 10, TM_JOURNAL_HEADER_SIZE);
    tm_put_u16(output + 12, TM_JOURNAL_RECORD_SIZE);
    return true;
}

static bool tm_header_valid(const uint8_t input[TM_JOURNAL_HEADER_SIZE])
{
    return memcmp(input, tm_journal_magic, sizeof(tm_journal_magic)) == 0 &&
           tm_get_u16(input + 8) == TM_JOURNAL_VERSION &&
           tm_get_u16(input + 10) == TM_JOURNAL_HEADER_SIZE &&
           tm_get_u16(input + 12) == TM_JOURNAL_RECORD_SIZE && tm_get_u16(input + 14) == 0;
}

static bool tm_record_encode(const tm_journal_event *event,
                             uint8_t output[TM_JOURNAL_RECORD_SIZE])
{
    size_t device_length;
    size_t ip_length;
    if (event == NULL || event->timestamp < 0 || !tm_family_is_valid(event->family) ||
        (ip_length = tm_ip_size(event->family)) == 0) {
        return false;
    }
    device_length = tm_bounded_string_length(event->device_id, TM_JOURNAL_DEVICE_ID_SIZE);
    if (device_length >= TM_JOURNAL_DEVICE_ID_SIZE) {
        return false;
    }
    memset(output, 0, TM_JOURNAL_RECORD_SIZE);
    memcpy(output, tm_record_magic, sizeof(tm_record_magic));
    tm_put_u16(output + 4, TM_JOURNAL_VERSION);
    tm_put_u16(output + 6, TM_JOURNAL_RECORD_SIZE);
    tm_put_u64(output + 8, (uint64_t)event->timestamp);
    output[16] = event->family;
    output[17] = event->protocol;
    tm_put_u16(output + 18, event->src_port);
    tm_put_u16(output + 20, event->dst_port);
    output[22] = (uint8_t)device_length;
    memcpy(output + 24, event->src_ip.bytes, ip_length);
    memcpy(output + 40, event->dst_ip.bytes, ip_length);
    tm_put_u64(output + 56, event->rx_bytes);
    tm_put_u64(output + 64, event->tx_bytes);
    memcpy(output + 72, event->device_id, device_length);
    tm_put_u32(output + TM_JOURNAL_RECORD_PAYLOAD,
               tm_crc32(output, TM_JOURNAL_RECORD_PAYLOAD));
    return true;
}

static bool tm_record_decode(const uint8_t input[TM_JOURNAL_RECORD_SIZE],
                             tm_journal_event *event)
{
    size_t device_length = input[22];
    size_t ip_length;
    if (memcmp(input, tm_record_magic, sizeof(tm_record_magic)) != 0 ||
        tm_get_u16(input + 4) != TM_JOURNAL_VERSION ||
        tm_get_u16(input + 6) != TM_JOURNAL_RECORD_SIZE ||
        tm_get_u32(input + TM_JOURNAL_RECORD_PAYLOAD) !=
            tm_crc32(input, TM_JOURNAL_RECORD_PAYLOAD) ||
        !tm_family_is_valid(input[16]) || device_length >= TM_JOURNAL_DEVICE_ID_SIZE ||
        (ip_length = tm_ip_size(input[16])) == 0) {
        return false;
    }
    memset(event, 0, sizeof(*event));
    event->timestamp = (int64_t)tm_get_u64(input + 8);
    if (event->timestamp < 0) {
        return false;
    }
    event->family = input[16];
    event->protocol = input[17];
    event->src_port = tm_get_u16(input + 18);
    event->dst_port = tm_get_u16(input + 20);
    memcpy(event->src_ip.bytes, input + 24, ip_length);
    memcpy(event->dst_ip.bytes, input + 40, ip_length);
    event->rx_bytes = tm_get_u64(input + 56);
    event->tx_bytes = tm_get_u64(input + 64);
    memcpy(event->device_id, input + 72, device_length);
    event->device_id[device_length] = '\0';
    return true;
}

static bool tm_sync_stream(FILE *stream)
{
    return stream != NULL && fflush(stream) == 0 && TM_FSYNC(TM_FILENO(stream)) == 0;
}

static bool tm_truncate_stream(FILE *stream, long length)
{
    if (stream == NULL || length < 0 || fflush(stream) != 0) {
        return false;
    }
#ifdef _WIN32
    return _chsize_s(TM_FILENO(stream), (size_t)length) == 0;
#else
    return ftruncate(TM_FILENO(stream), (off_t)length) == 0;
#endif
}

static bool tm_recover_stream(FILE *stream)
{
    uint8_t header[TM_JOURNAL_HEADER_SIZE];
    uint8_t record[TM_JOURNAL_RECORD_SIZE];
    long length;
    long valid_length = TM_JOURNAL_HEADER_SIZE;
    if (fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) < 0) {
        return false;
    }
    if (length == 0) {
        tm_header_encode(header);
        return fseek(stream, 0, SEEK_SET) == 0 &&
               fwrite(header, 1, sizeof(header), stream) == sizeof(header) &&
               tm_sync_stream(stream);
    }
    if (length < TM_JOURNAL_HEADER_SIZE || fseek(stream, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), stream) != sizeof(header) || !tm_header_valid(header)) {
        errno = EINVAL;
        return false;
    }
    while (valid_length + TM_JOURNAL_RECORD_SIZE <= length) {
        tm_journal_event event;
        if (fread(record, 1, sizeof(record), stream) != sizeof(record) ||
            !tm_record_decode(record, &event)) {
            break;
        }
        valid_length += TM_JOURNAL_RECORD_SIZE;
    }
    if (valid_length != length && !tm_truncate_stream(stream, valid_length)) {
        return false;
    }
    return fseek(stream, 0, SEEK_END) == 0;
}

static bool tm_close_stream(tm_journal *journal)
{
    bool success = true;
    if (journal->stream != NULL) {
        success = tm_sync_stream(journal->stream);
        if (fclose(journal->stream) != 0) {
            success = false;
        }
        journal->stream = NULL;
        journal->shard_path[0] = '\0';
    }
    return success;
}

static void tm_abandon_stream(tm_journal *journal)
{
    if (journal->stream != NULL) {
        (void)fclose(journal->stream);
        journal->stream = NULL;
        journal->shard_path[0] = '\0';
    }
}

static bool tm_open_shard(tm_journal *journal, int64_t timestamp)
{
    char path[TM_PATH_MAX];
    if (!tm_journal_shard_path(journal->directory, timestamp, path)) {
        errno = EINVAL;
        return false;
    }
    if (journal->stream != NULL && strcmp(path, journal->shard_path) == 0) {
        return true;
    }
    if (!tm_close_stream(journal)) {
        return false;
    }
    journal->stream = fopen(path, "a+b");
    if (journal->stream == NULL || !tm_recover_stream(journal->stream)) {
        if (journal->stream != NULL) {
            fclose(journal->stream);
            journal->stream = NULL;
        }
        return false;
    }
    memcpy(journal->shard_path, path, strlen(path) + 1);
    return true;
}

bool tm_journal_init(tm_journal *journal, const char *directory)
{
    size_t length;
    if (journal == NULL) {
        return false;
    }
    memset(journal, 0, sizeof(*journal));
    if (directory == NULL || directory[0] == '\0' ||
        (length = strlen(directory)) >= TM_PATH_MAX || !tm_storage_ensure_directory(directory)) {
        return false;
    }
    journal->queue = calloc(TM_JOURNAL_QUEUE_LIMIT, sizeof(*journal->queue));
    if (journal->queue == NULL) {
        return false;
    }
    memcpy(journal->directory, directory, length + 1);
    journal->enabled = true;
    journal->next_flush = time(NULL) + TM_JOURNAL_FLUSH_SECONDS;
    return true;
}

void tm_journal_destroy(tm_journal *journal)
{
    if (journal == NULL) {
        return;
    }
    if (journal->enabled && !tm_journal_flush(journal)) {
        tm_journal_error(journal, "shutdown flush");
    }
    if (!tm_close_stream(journal)) {
        tm_journal_error(journal, "close");
    }
    free(journal->queue);
    memset(journal, 0, sizeof(*journal));
}

bool tm_journal_enqueue(tm_journal *journal, const tm_journal_event *event)
{
    uint8_t validation[TM_JOURNAL_RECORD_SIZE];
    if (journal == NULL || !journal->enabled || journal->queue == NULL || event == NULL ||
        !tm_record_encode(event, validation)) {
        return false;
    }
    if (journal->queue_count >= TM_JOURNAL_QUEUE_LIMIT) {
        errno = ENOSPC;
        tm_journal_error(journal, "queue full; event dropped");
        return false;
    }
    journal->queue[journal->queue_count++] = *event;
    return true;
}

bool tm_journal_flush(tm_journal *journal)
{
    uint8_t record[TM_JOURNAL_RECORD_SIZE];
    bool success = true;
    if (journal == NULL || !journal->enabled) {
        return true;
    }
    for (size_t index = 0; index < journal->queue_count; index++) {
        if (!tm_open_shard(journal, journal->queue[index].timestamp) ||
            !tm_record_encode(&journal->queue[index], record) ||
            fwrite(record, 1, sizeof(record), journal->stream) != sizeof(record)) {
            tm_journal_error(journal, "write");
            success = false;
            break;
        }
    }
    if (success && journal->queue_count != 0 && !tm_sync_stream(journal->stream)) {
        tm_journal_error(journal, "sync");
        success = false;
    }
    if (!success) {
        tm_abandon_stream(journal);
    }
    journal->queue_count = 0;
    journal->next_flush = time(NULL) + TM_JOURNAL_FLUSH_SECONDS;
    if (success) {
        journal->error_reported = false;
    }
    return success;
}

void tm_journal_tick(tm_journal *journal, time_t now)
{
    if (journal != NULL && journal->enabled && journal->queue_count != 0 &&
        now >= journal->next_flush) {
        (void)tm_journal_flush(journal);
    }
}

bool tm_journal_event_from_flow(const tm_raw_flow *flow, const tm_flow_delta *delta,
                                time_t timestamp, tm_journal_event *event)
{
    char ip[46];
    if (flow == NULL || delta == NULL || event == NULL || timestamp < 0 ||
        !tm_family_is_valid(flow->family)) {
        return false;
    }
    memset(event, 0, sizeof(*event));
    event->timestamp = (int64_t)timestamp;
    event->family = flow->family;
    event->protocol = flow->l4_proto;
    event->src_ip = flow->orig_src;
    event->dst_ip = flow->orig_dst;
    event->src_port = flow->orig_src_port;
    event->dst_port = flow->orig_dst_port;
    event->rx_bytes = delta->rx_bytes;
    event->tx_bytes = delta->tx_bytes;
    if (delta->mac_known) {
        (void)snprintf(event->device_id, sizeof(event->device_id),
                       "%02x:%02x:%02x:%02x:%02x:%02x", delta->mac[0], delta->mac[1],
                       delta->mac[2], delta->mac[3], delta->mac[4], delta->mac[5]);
    } else if (tm_ip_format(delta->family, &delta->host_ip, ip, sizeof(ip))) {
        (void)snprintf(event->device_id, sizeof(event->device_id), "ip%u:%s", delta->family,
                       ip);
    }
    return true;
}

bool tm_journal_read_file(const char *path, tm_journal_event *events, size_t capacity,
                          size_t *count)
{
    FILE *stream;
    uint8_t header[TM_JOURNAL_HEADER_SIZE];
    uint8_t record[TM_JOURNAL_RECORD_SIZE];
    size_t used = 0;
    bool success = true;
    if (path == NULL || events == NULL || count == NULL) {
        return false;
    }
    *count = 0;
    stream = fopen(path, "rb");
    if (stream == NULL || fread(header, 1, sizeof(header), stream) != sizeof(header) ||
        !tm_header_valid(header)) {
        if (stream != NULL) {
            fclose(stream);
        }
        return false;
    }
    for (;;) {
        size_t amount = fread(record, 1, sizeof(record), stream);
        if (amount == 0 && feof(stream)) {
            break;
        }
        if (amount != sizeof(record)) {
            success = false;
            break;
        }
        if (used >= capacity || !tm_record_decode(record, &events[used])) {
            success = false;
            break;
        }
        used++;
    }
    if (success && ferror(stream)) {
        success = false;
    }
    if (fclose(stream) != 0) {
        success = false;
    }
    if (success) {
        *count = used;
    }
    return success;
}

tm_journal_scan_result tm_journal_scan_file(const char *path, tm_journal_visitor visitor,
                                            void *context, tm_journal_scan_stats *stats)
{
    FILE *stream;
    uint8_t header[TM_JOURNAL_HEADER_SIZE];
    uint8_t record[TM_JOURNAL_RECORD_SIZE];
    tm_journal_scan_stats result_stats;
    tm_journal_scan_result result = TM_JOURNAL_SCAN_OK;
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    memset(&result_stats, 0, sizeof(result_stats));
    if (path == NULL || visitor == NULL) {
        return TM_JOURNAL_SCAN_IO_ERROR;
    }
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return errno == ENOENT ? TM_JOURNAL_SCAN_NOT_FOUND : TM_JOURNAL_SCAN_IO_ERROR;
    }
    if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
        !tm_header_valid(header)) {
        result = ferror(stream) ? TM_JOURNAL_SCAN_IO_ERROR : TM_JOURNAL_SCAN_INVALID_HEADER;
    } else {
        for (;;) {
            tm_journal_event event;
            size_t amount = fread(record, 1, sizeof(record), stream);
            if (amount == 0 && feof(stream)) {
                break;
            }
            if (amount != sizeof(record)) {
                if (ferror(stream)) {
                    result = TM_JOURNAL_SCAN_IO_ERROR;
                } else {
                    result_stats.corrupt_records++;
                }
                break;
            }
            if (!tm_record_decode(record, &event)) {
                result_stats.corrupt_records++;
                continue;
            }
            if (!visitor(&event, context)) {
                result = TM_JOURNAL_SCAN_VISITOR_ERROR;
                break;
            }
            result_stats.valid_records++;
        }
    }
    if (fclose(stream) != 0 && result == TM_JOURNAL_SCAN_OK) {
        result = TM_JOURNAL_SCAN_IO_ERROR;
    }
    if (stats != NULL) {
        *stats = result_stats;
    }
    return result;
}
