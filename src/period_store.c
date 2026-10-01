#include "period_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct {
    tm_period_descriptor descriptor;
    char path[TM_PATH_MAX];
} tm_archive_entry;

static bool tm_join_path(const char *directory, const char *name, char output[TM_PATH_MAX])
{
    int written = snprintf(output, TM_PATH_MAX, "%s/%s", directory, name);
    return written > 0 && written < TM_PATH_MAX;
}

static bool tm_current_path(const tm_period_store *store, char output[TM_PATH_MAX])
{
    return tm_join_path(store->directory, "current.db", output);
}

static bool tm_archive_path(const tm_period_store *store, tm_date start, char output[TM_PATH_MAX])
{
    char name[32];
    uint32_t encoded = tm_date_encode(start);
    int written;
    if (encoded == 0) {
        return false;
    }
    written = snprintf(name, sizeof(name), "period-%08u.db", encoded);
    return written > 0 && (size_t)written < sizeof(name) && tm_join_path(store->directory, name, output);
}

static bool tm_period_equal(tm_period left, tm_period right)
{
    return tm_date_encode(left.start) == tm_date_encode(right.start) &&
           tm_date_encode(left.end) == tm_date_encode(right.end);
}

static bool tm_valid_archive_name(const char *name, tm_date *start)
{
    uint32_t encoded = 0;
    size_t index;
    if (name == NULL || strlen(name) != 18 || strncmp(name, "period-", 7) != 0 ||
        strcmp(name + 15, ".db") != 0) {
        return false;
    }
    for (index = 7; index < 15; index++) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
        encoded = encoded * 10U + (uint32_t)(name[index] - '0');
    }
    return tm_date_decode(encoded, start);
}

static int tm_archive_compare_desc(const void *left_pointer, const void *right_pointer)
{
    const tm_archive_entry *left = left_pointer;
    const tm_archive_entry *right = right_pointer;
    uint32_t left_start = tm_date_encode(left->descriptor.period.start);
    uint32_t right_start = tm_date_encode(right->descriptor.period.start);
    return left_start > right_start ? -1 : left_start < right_start ? 1 : 0;
}

static bool tm_consider_archive(const tm_period_store *store, const char *name,
                                tm_archive_entry *entries, size_t *count)
{
    tm_date filename_start;
    tm_database_metadata metadata;
    char path[TM_PATH_MAX];
    if (!tm_valid_archive_name(name, &filename_start)) {
        return true;
    }
    if (*count >= TM_PERIOD_LIST_MAX) {
        return false;
    }
    if (!tm_join_path(store->directory, name, path) ||
        tm_storage_validate_file(path, 0, &metadata) != TM_DATABASE_OK ||
        metadata.protocol_fingerprint != store->protocol_fingerprint ||
        tm_date_encode(filename_start) != tm_date_encode(metadata.period.start)) {
        return true;
    }
    entries[*count].descriptor.period = metadata.period;
    entries[*count].descriptor.current = false;
    entries[*count].descriptor.record_count = metadata.record_count;
    memcpy(entries[*count].path, path, strlen(path) + 1);
    (*count)++;
    return true;
}

static bool tm_scan_archives(const tm_period_store *store, tm_archive_entry *entries, size_t *count)
{
    *count = 0;
#ifdef _WIN32
    char pattern[TM_PATH_MAX];
    WIN32_FIND_DATAA item;
    HANDLE handle;
    DWORD find_error;
    if (!tm_join_path(store->directory, "*.db", pattern)) {
        return false;
    }
    handle = FindFirstFileA(pattern, &item);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    do {
        if ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            !tm_consider_archive(store, item.cFileName, entries, count)) {
            FindClose(handle);
            return false;
        }
    } while (FindNextFileA(handle, &item) != 0);
    find_error = GetLastError();
    FindClose(handle);
    return find_error == ERROR_NO_MORE_FILES;
#else
    DIR *directory = opendir(store->directory);
    struct dirent *item;
    if (directory == NULL) {
        return false;
    }
    while ((item = readdir(directory)) != NULL) {
        if (!tm_consider_archive(store, item->d_name, entries, count)) {
            closedir(directory);
            return false;
        }
    }
    return closedir(directory) == 0;
#endif
}

bool tm_period_store_cleanup(tm_period_store *store)
{
    tm_archive_entry *entries;
    size_t count;
    size_t index;
    if (store == NULL || !store->initialized) {
        return false;
    }
    entries = calloc(TM_PERIOD_LIST_MAX, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }
    if (!tm_scan_archives(store, entries, &count)) {
        free(entries);
        return false;
    }
    qsort(entries, count, sizeof(*entries), tm_archive_compare_desc);
    for (index = store->generations; index < count; index++) {
        if (remove(entries[index].path) != 0) {
            free(entries);
            return false;
        }
    }
    free(entries);
    return true;
}

static bool tm_initialize_empty(tm_period_store *store, tm_period period)
{
    memset(&store->aggregator, 0, sizeof(store->aggregator));
    if (!tm_aggregator_init(&store->aggregator, store->record_limit)) {
        return false;
    }
    memset(&store->metadata, 0, sizeof(store->metadata));
    store->metadata.period = period;
    store->metadata.protocol_fingerprint = store->protocol_fingerprint;
    return true;
}

bool tm_period_store_commit(tm_period_store *store)
{
    char path[TM_PATH_MAX];
    if (store == NULL || !store->initialized || !tm_current_path(store, path)) {
        return false;
    }
    store->metadata.record_count = (uint32_t)store->aggregator.count;
    return tm_storage_commit_database(path, &store->aggregator, &store->metadata);
}

static bool tm_archive_current(tm_period_store *store)
{
    char path[TM_PATH_MAX];
    store->metadata.record_count = (uint32_t)store->aggregator.count;
    return tm_archive_path(store, store->metadata.period.start, path) &&
           tm_storage_commit_database(path, &store->aggregator, &store->metadata);
}

bool tm_period_store_rollover(tm_period_store *store, tm_date current_date, bool *rolled_over)
{
    tm_period new_period;
    tm_aggregator old_aggregator;
    tm_database_metadata old_metadata;
    if (rolled_over != NULL) {
        *rolled_over = false;
    }
    if (store == NULL || !store->initialized ||
        !tm_period_for_date(&store->rule, current_date, &new_period)) {
        return false;
    }
    if (tm_period_equal(store->metadata.period, new_period)) {
        return true;
    }
    if (tm_date_encode(new_period.start) < tm_date_encode(store->metadata.period.start)) {
        return true;
    }
    if (!tm_archive_current(store)) {
        return false;
    }
    old_aggregator = store->aggregator;
    old_metadata = store->metadata;
    if (!tm_initialize_empty(store, new_period)) {
        store->aggregator = old_aggregator;
        store->metadata = old_metadata;
        return false;
    }
    if (!tm_period_store_commit(store)) {
        tm_aggregator_destroy(&store->aggregator);
        store->aggregator = old_aggregator;
        store->metadata = old_metadata;
        return false;
    }
    tm_aggregator_destroy(&old_aggregator);
    if (!tm_period_store_cleanup(store)) {
        return false;
    }
    if (rolled_over != NULL) {
        *rolled_over = true;
    }
    return true;
}

bool tm_period_store_init(tm_period_store *store, const char *directory,
                          const tm_period_rule *rule, tm_date current_date,
                          size_t record_limit, size_t generations,
                          uint64_t protocol_fingerprint)
{
    tm_period expected_period;
    char current_path[TM_PATH_MAX];
    tm_aggregator loaded;
    tm_database_metadata loaded_metadata;
    tm_database_result load_result;

    if (store == NULL || directory == NULL || rule == NULL || strlen(directory) >= TM_PATH_MAX ||
        protocol_fingerprint == 0 || !tm_period_for_date(rule, current_date, &expected_period) ||
        !tm_storage_ensure_directory(directory)) {
        return false;
    }
    memset(store, 0, sizeof(*store));
    memcpy(store->directory, directory, strlen(directory) + 1);
    store->rule = *rule;
    store->record_limit = record_limit;
    store->generations = generations;
    store->protocol_fingerprint = protocol_fingerprint;
    store->initialized = true;
    if (!tm_current_path(store, current_path)) {
        tm_period_store_destroy(store);
        return false;
    }
    if (tm_storage_file_exists(current_path)) {
        load_result = tm_storage_load_database(current_path, record_limit, &loaded, &loaded_metadata);
        if (load_result != TM_DATABASE_OK) {
            tm_period_store_destroy(store);
            return false;
        }
        if (loaded_metadata.protocol_fingerprint != protocol_fingerprint) {
            tm_aggregator_destroy(&loaded);
            tm_period_store_destroy(store);
            return false;
        }
        store->aggregator = loaded;
        store->metadata = loaded_metadata;
        if (!tm_period_equal(loaded_metadata.period, expected_period) &&
            tm_date_encode(expected_period.start) >
                tm_date_encode(loaded_metadata.period.start)) {
            bool rolled_over;
            if (!tm_period_store_rollover(store, current_date, &rolled_over)) {
                tm_period_store_destroy(store);
                return false;
            }
        }
    } else {
        if (!tm_initialize_empty(store, expected_period) || !tm_period_store_commit(store)) {
            tm_period_store_destroy(store);
            return false;
        }
    }
    if (!tm_period_store_cleanup(store)) {
        tm_period_store_destroy(store);
        return false;
    }
    return true;
}

void tm_period_store_destroy(tm_period_store *store)
{
    if (store != NULL) {
        if (store->aggregator.entries != NULL) {
            tm_aggregator_destroy(&store->aggregator);
        }
        memset(store, 0, sizeof(*store));
    }
}

bool tm_period_store_list(const tm_period_store *store, tm_period_descriptor **periods,
                          size_t *count)
{
    tm_archive_entry *archives;
    size_t archive_count;
    tm_period_descriptor *result;
    if (store == NULL || periods == NULL || count == NULL || !store->initialized) {
        return false;
    }
    *periods = NULL;
    *count = 0;
    archives = calloc(TM_PERIOD_LIST_MAX, sizeof(*archives));
    if (archives == NULL) {
        return false;
    }
    if (!tm_scan_archives(store, archives, &archive_count)) {
        free(archives);
        return false;
    }
    qsort(archives, archive_count, sizeof(*archives), tm_archive_compare_desc);
    result = calloc(archive_count + 1, sizeof(*result));
    if (result == NULL) {
        free(archives);
        return false;
    }
    result[0].period = store->metadata.period;
    result[0].current = true;
    result[0].record_count = (uint32_t)store->aggregator.count;
    for (size_t index = 0; index < archive_count; index++) {
        result[index + 1] = archives[index].descriptor;
    }
    free(archives);
    *periods = result;
    *count = archive_count + 1;
    return true;
}

tm_database_result tm_period_store_load(const tm_period_store *store, tm_date start,
                                        tm_aggregator *aggregator,
                                        tm_database_metadata *metadata)
{
    char path[TM_PATH_MAX];
    if (store == NULL || aggregator == NULL || metadata == NULL || !store->initialized ||
        !tm_date_is_valid(start)) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    if (tm_date_encode(start) == tm_date_encode(store->metadata.period.start)) {
        if (!tm_current_path(store, path)) {
            return TM_DATABASE_INVALID_ARGUMENT;
        }
    } else if (!tm_archive_path(store, start, path)) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    {
        tm_database_result result =
            tm_storage_load_database(path, store->record_limit, aggregator, metadata);
        if (result == TM_DATABASE_OK &&
            metadata->protocol_fingerprint != store->protocol_fingerprint) {
            tm_aggregator_destroy(aggregator);
            memset(aggregator, 0, sizeof(*aggregator));
            memset(metadata, 0, sizeof(*metadata));
            return TM_DATABASE_PROTOCOL_MISMATCH;
        }
        return result;
    }
}
