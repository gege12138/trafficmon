#ifndef TRAFFICMON_RETENTION_H
#define TRAFFICMON_RETENTION_H

#include "period.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t entries_scanned;
    uint64_t journal_files;
    uint64_t deleted_files;
    uint64_t retained_files;
    uint64_t ignored_entries;
    uint64_t delete_failures;
} tm_retention_stats;

/*
 * Delete journal shards whose file date is more than retention_days before today.
 * Only exact journal-YYYYMMDD.bin names are ever considered for deletion.
 */
bool tm_retention_cleanup(const char *directory, uint32_t retention_days, tm_date today,
                          tm_retention_stats *stats);

#endif
