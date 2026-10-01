#ifndef TRAFFICMON_STORAGE_H
#define TRAFFICMON_STORAGE_H

#include "database.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TM_PATH_MAX = 512 };

bool tm_storage_ensure_directory(const char *path);

/* Writes and fsyncs <final_path>.tmp without replacing final_path. */
bool tm_storage_write_temp(const char *final_path, const uint8_t *data, size_t length);
/* Atomically replaces final_path with its already-fsynced .tmp sibling. */
bool tm_storage_replace_temp(const char *final_path);

bool tm_storage_commit_database(const char *final_path, const tm_aggregator *aggregator,
                                const tm_database_metadata *metadata);
tm_database_result tm_storage_load_database(const char *path, size_t record_limit,
                                            tm_aggregator *aggregator,
                                            tm_database_metadata *metadata);
tm_database_result tm_storage_validate_file(const char *path, size_t record_limit,
                                            tm_database_metadata *metadata);
bool tm_storage_file_exists(const char *path);

#endif
