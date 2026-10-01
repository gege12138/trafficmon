#ifndef TRAFFICMON_FLOW_H
#define TRAFFICMON_FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TM_FAMILY_IPV4 = 4,
    TM_FAMILY_IPV6 = 6,
    TM_MAC_LENGTH = 6,
    TM_IPV6_LENGTH = 16,
};

typedef struct {
    uint8_t bytes[TM_IPV6_LENGTH];
} tm_ip_addr;

typedef struct {
    uint8_t family;
    uint8_t l4_proto;
    uint16_t service_port;
    tm_ip_addr host_ip;
    uint8_t mac[TM_MAC_LENGTH];
    bool mac_known;
    bool new_connection;
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t tx_packets;
} tm_flow_delta;

typedef struct {
    uint64_t bytes;
    uint64_t packets;
} tm_counter;

typedef struct {
    uint8_t family;
    uint8_t l4_proto;
    tm_ip_addr orig_src;
    tm_ip_addr orig_dst;
    uint16_t orig_src_port;
    uint16_t orig_dst_port;
    tm_counter original;
    tm_counter reply;
    uint8_t host_mac[TM_MAC_LENGTH];
    bool mac_known;
    bool new_connection;
} tm_raw_flow;

bool tm_family_is_valid(uint8_t family);
size_t tm_ip_size(uint8_t family);

#endif
