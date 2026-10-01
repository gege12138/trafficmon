#ifndef TRAFFICMON_PERIOD_STORE_H
#define TRAFFICMON_PERIOD_STORE_H

#include "aggregator.h"
#include "database.h"
#include "period.h"
#include "storage.h"

#include <stdbool.h>
#include <stddef.h>

enum { TM_PERIOD_LIST_MAX = 1024 };

typedef struct {
    tm_period period;
    bool current;
    uint32_t record_count;
} tm_period_descriptor;

typedef struct {
    char directory[TM_PATH_MAX];
    tm_period_rule rule;
    tm_database_metadata metadata;
    tm_aggregator aggregator;
    size_t record_limit;
    size_t generations;
    uint64_t protocol_fingerprint;
    bool initialized;
} tm_period_store;

bool tm_period_store_init(tm_period_store *store, const char *directory,
                          const tm_period_rule *rule, tm_date current_date,
                          size_t record_limit, size_t generations,
                          uint64_t protocol_fingerprint);
void tm_period_store_destroy(tm_period_store *store);
bool tm_period_store_commit(tm_period_store *store);
bool tm_period_store_rollover(tm_period_store *store, tm_date current_date, bool *rolled_over);
bool tm_period_store_cleanup(tm_period_store *store);

/* Caller owns *periods and releases it with free(). */
bool tm_period_store_list(const tm_period_store *store, tm_period_descriptor **periods,
                          size_t *count);
/* start == current period start returns a separate loaded copy, never a borrowed pointer. */
tm_database_result tm_period_store_load(const tm_period_store *store, tm_date start,
                                        tm_aggregator *aggregator,
                                        tm_database_metadata *metadata);

#endif
