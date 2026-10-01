#ifndef TRAFFICMON_AGGREGATOR_H
#define TRAFFICMON_AGGREGATOR_H

#include "flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t family;
    tm_ip_addr host_ip;
    uint8_t mac[TM_MAC_LENGTH];
    bool mac_known;
    uint16_t protocol_id;
} tm_aggregate_key;

typedef struct {
    tm_aggregate_key key;
    uint64_t connections;
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t tx_packets;
    bool occupied;
} tm_aggregate_entry;

typedef struct {
    tm_aggregate_entry *entries;
    size_t capacity;
    size_t count;
    size_t limit;
    uint64_t dropped_new_keys;
} tm_aggregator;

bool tm_aggregator_init(tm_aggregator *aggregator, size_t limit);
void tm_aggregator_destroy(tm_aggregator *aggregator);
bool tm_aggregator_add(tm_aggregator *aggregator, const tm_flow_delta *delta,
                       uint16_t protocol_id);
bool tm_aggregator_restore_entry(tm_aggregator *aggregator, const tm_aggregate_entry *entry);
const tm_aggregate_entry *tm_aggregator_find(const tm_aggregator *aggregator,
                                             const tm_aggregate_key *key);
bool tm_aggregator_resolve_address(tm_aggregator *aggregator, uint8_t family,
                                   const tm_ip_addr *ip,
                                   const uint8_t mac[TM_MAC_LENGTH], size_t *merged);

#endif
