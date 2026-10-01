#ifndef TRAFFICMON_QUERY_H
#define TRAFFICMON_QUERY_H

#include "aggregator.h"
#include "identity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TM_GROUP_FAMILY = 1U << 0,
    TM_GROUP_IP = 1U << 1,
    TM_GROUP_MAC = 1U << 2,
    TM_GROUP_PROTOCOL = 1U << 3,
    TM_GROUP_DEVICE = 1U << 4,
};

typedef enum {
    TM_ORDER_TOTAL_BYTES,
    TM_ORDER_RX_BYTES,
    TM_ORDER_TX_BYTES,
    TM_ORDER_CONNECTIONS,
} tm_query_order;

typedef struct {
    uint32_t group_by;
    tm_query_order order;
    bool descending;
    size_t limit;
    size_t offset;
} tm_query_spec;

typedef struct {
    tm_aggregate_key key;
    uint64_t connections;
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t tx_packets;
} tm_query_row;

typedef struct {
    tm_query_row *rows;
    size_t count;
    size_t total_count;
} tm_query_result;

typedef bool (*tm_query_predicate)(const tm_query_row *row, void *context);

bool tm_query_run(const tm_aggregator *aggregator, const tm_query_spec *spec,
                  tm_query_result *result);
bool tm_query_run_with_identity(const tm_aggregator *aggregator, const tm_query_spec *spec,
                                tm_identity *identity, tm_query_result *result);
bool tm_query_run_filtered(const tm_aggregator *aggregator, const tm_query_spec *spec,
                           tm_identity *identity, tm_query_predicate predicate, void *context,
                           tm_query_result *result);
void tm_query_result_destroy(tm_query_result *result);

#endif
