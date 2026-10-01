#ifndef TRAFFICMON_CONNTRACK_PARSER_H
#define TRAFFICMON_CONNTRACK_PARSER_H

#include "flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    tm_raw_flow flow;
    tm_ip_addr reply_src;
    tm_ip_addr reply_dst;
    uint16_t reply_src_port;
    uint16_t reply_dst_port;
} tm_conntrack_record;

/* Parses one complete nlmsghdr. It never retains pointers into input. */
bool tm_conntrack_parse_message(const uint8_t *data, size_t length,
                                tm_conntrack_record *record);

#endif
