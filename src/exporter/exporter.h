#ifndef TRAFFICMON_EXPORTER_H
#define TRAFFICMON_EXPORTER_H

#include "period.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *journal_directory;
    const char *output_path;
    tm_date start_date;
    tm_date end_date;
} tm_export_options;

typedef struct {
    uint64_t exported_records;
    uint64_t corrupt_records;
    uint32_t files_read;
    uint32_t missing_files;
    uint32_t invalid_files;
} tm_export_stats;

/* Exports an inclusive UTC date range with constant memory usage. */
bool tm_export_csv(const tm_export_options *options, tm_export_stats *stats);

#endif
