#ifndef TRAFFICMON_COLLECTOR_H
#define TRAFFICMON_COLLECTOR_H

#include "aggregator.h"
#include "identity.h"
#include "protocol_db.h"
#include "subnet.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    bool enabled;
    bool degraded;
    size_t receive_buffer;
    uint64_t netlink_messages;
    uint64_t netlink_parse_errors;
    uint64_t netlink_enobufs;
    uint64_t conntrack_new_events;
    uint64_t conntrack_destroy_events;
    uint64_t conntrack_dump_count;
    uint64_t conntrack_dump_errors;
    time_t last_successful_dump;
} tm_collector_stats;

typedef struct {
    int socket_fd;
    uint32_t sequence;
    uint32_t dump_sequence;
    unsigned int refresh_seconds;
    time_t next_dump;
    tm_aggregator *aggregator;
    const tm_subnet_set *local_networks;
    const tm_protocol_db *protocols;
    tm_identity *identity;
    tm_collector_stats stats;
    bool dump_active;
} tm_collector;

bool tm_collector_init(tm_collector *collector, tm_aggregator *aggregator,
                       const tm_subnet_set *local_networks,
                       const tm_protocol_db *protocols, tm_identity *identity, size_t receive_buffer,
                       unsigned int refresh_seconds);
void tm_collector_destroy(tm_collector *collector);
bool tm_collector_tick(tm_collector *collector);
const char *tm_collector_state(const tm_collector *collector);

#endif
