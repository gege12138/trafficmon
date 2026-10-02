#ifndef TRAFFICMON_JOURNAL_H
#define TRAFFICMON_JOURNAL_H

#include "flow.h"
#include "storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum {
    TM_JOURNAL_DEVICE_ID_SIZE = 52,
    TM_JOURNAL_HEADER_SIZE = 16,
    TM_JOURNAL_RECORD_SIZE = 128,
    TM_JOURNAL_QUEUE_LIMIT = 256,
};

typedef struct {
    int64_t timestamp;
    uint8_t family;
    uint8_t protocol;
    tm_ip_addr src_ip;
    tm_ip_addr dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    char device_id[TM_JOURNAL_DEVICE_ID_SIZE];
} tm_journal_event;

typedef struct {
    char directory[TM_PATH_MAX];
    char shard_path[TM_PATH_MAX];
    FILE *stream;
    tm_journal_event *queue;
    size_t queue_count;
    time_t next_flush;
    bool enabled;
    bool error_reported;
} tm_journal;

typedef bool (*tm_journal_visitor)(const tm_journal_event *event, void *context);

typedef struct {
    uint64_t valid_records;
    uint64_t corrupt_records;
} tm_journal_scan_stats;

typedef enum {
    TM_JOURNAL_SCAN_OK,
    TM_JOURNAL_SCAN_NOT_FOUND,
    TM_JOURNAL_SCAN_INVALID_HEADER,
    TM_JOURNAL_SCAN_IO_ERROR,
    TM_JOURNAL_SCAN_VISITOR_ERROR,
} tm_journal_scan_result;

bool tm_journal_init(tm_journal *journal, const char *directory);
void tm_journal_destroy(tm_journal *journal);

/* Enqueue is memory-only. A full queue drops the new event instead of blocking accounting. */
bool tm_journal_enqueue(tm_journal *journal, const tm_journal_event *event);
/* Flushes a bounded batch. Failures are logged and do not alter daemon liveness. */
bool tm_journal_flush(tm_journal *journal);
void tm_journal_tick(tm_journal *journal, time_t now);

bool tm_journal_event_from_flow(const tm_raw_flow *flow, const tm_flow_delta *delta,
                                time_t timestamp, tm_journal_event *event);
bool tm_journal_shard_path(const char *directory, int64_t timestamp,
                           char output[TM_PATH_MAX]);

/* Strict reader used by recovery tests and future offline tooling. */
bool tm_journal_read_file(const char *path, tm_journal_event *events, size_t capacity,
                          size_t *count);

/* Constant-memory reader. Invalid records are counted and skipped at fixed record boundaries. */
tm_journal_scan_result tm_journal_scan_file(const char *path, tm_journal_visitor visitor,
                                            void *context, tm_journal_scan_stats *stats);

#endif
