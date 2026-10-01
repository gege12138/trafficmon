#ifndef TRAFFICMON_IDENTITY_H
#define TRAFFICMON_IDENTITY_H

#include "flow.h"
#include "aggregator.h"
#include "subnet.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum {
    TM_NEIGHBOR_CACHE_LIMIT = 1024,
    TM_HOSTNAME_CACHE_LIMIT = 512,
    TM_UNRESOLVED_CACHE_LIMIT = 2048,
    TM_LOCAL_INTERFACE_LIMIT = 16,
    TM_IDENTITY_NAME_LENGTH = 64,
    TM_NEIGHBOR_TTL_SECONDS = 300,
    TM_IDENTITY_REFRESH_SECONDS = 30,
    TM_NEIGHBOR_REFRESH_SECONDS = 60,
    TM_UNRESOLVED_TTL_SECONDS = 600,
    TM_NEIGHBOR_DELETE_GRACE_SECONDS = 30,
};

typedef struct {
    uint8_t family;
    tm_ip_addr ip;
    uint8_t mac[TM_MAC_LENGTH];
    unsigned int ifindex;
    uint16_t nud_state;
    time_t seen_at;
    time_t invalid_at;
    bool occupied;
} tm_neighbor_entry;

typedef struct {
    uint8_t family;
    tm_ip_addr ip;
    time_t first_seen;
    time_t last_seen;
    bool occupied;
} tm_unresolved_entry;

typedef struct {
    uint8_t mac[TM_MAC_LENGTH];
    bool mac_known;
    uint8_t family;
    tm_ip_addr ip;
    char name[TM_IDENTITY_NAME_LENGTH];
    uint8_t priority;
    bool occupied;
} tm_hostname_entry;

typedef struct {
    tm_neighbor_entry *neighbors;
    tm_unresolved_entry *unresolved;
    tm_hostname_entry *hostnames;
    size_t neighbor_count;
    size_t unresolved_count;
    size_t hostname_count;
    unsigned int ifindices[TM_LOCAL_INTERFACE_LIMIT];
    size_t ifindex_count;
    const tm_subnet_set *local_networks;
    const char *static_config;
    const char *lease_file;
    time_t next_neighbor_refresh;
    time_t next_hostname_refresh;
    tm_aggregator *aggregator;
    int neighbor_socket_fd;
    bool debug;
} tm_identity;

bool tm_identity_init(tm_identity *identity, const tm_subnet_set *local_networks,
                      const char *static_config, const char *lease_file);
void tm_identity_destroy(tm_identity *identity);
void tm_identity_bind_aggregator(tm_identity *identity, tm_aggregator *aggregator, bool debug);
bool tm_identity_add_interface(tm_identity *identity, const char *name);
bool tm_identity_neighbor_put(tm_identity *identity, uint8_t family, const tm_ip_addr *ip,
                              const uint8_t mac[TM_MAC_LENGTH], time_t now);
bool tm_identity_neighbor_get(tm_identity *identity, uint8_t family, const tm_ip_addr *ip,
                              time_t now, uint8_t mac[TM_MAC_LENGTH]);
void tm_identity_expire(tm_identity *identity, time_t now);
bool tm_identity_observe_unresolved(tm_identity *identity, uint8_t family,
                                    const tm_ip_addr *ip, time_t now);
bool tm_identity_refresh_neighbors(tm_identity *identity, time_t now);
bool tm_identity_tick(tm_identity *identity, time_t now);
bool tm_identity_consume_neighbor_message(tm_identity *identity, const void *message,
                                           size_t length, time_t now);
void tm_identity_refresh_hostnames(tm_identity *identity, time_t now);
bool tm_identity_hostname_put(tm_identity *identity, const uint8_t mac[TM_MAC_LENGTH],
                              uint8_t family, const tm_ip_addr *ip, const char *name,
                              uint8_t priority);
const char *tm_identity_hostname(const tm_identity *identity, bool mac_known,
                                 const uint8_t mac[TM_MAC_LENGTH], uint8_t family,
                                 const tm_ip_addr *ip);

#endif
