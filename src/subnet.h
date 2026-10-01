#ifndef TRAFFICMON_SUBNET_H
#define TRAFFICMON_SUBNET_H

#include "flow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TM_SUBNET_SET_MAX = 64 };

typedef struct {
    uint8_t family;
    uint8_t prefix_length;
    tm_ip_addr network;
} tm_subnet;

typedef struct {
    tm_subnet items[TM_SUBNET_SET_MAX];
    size_t count;
} tm_subnet_set;

bool tm_ip_parse(uint8_t family, const char *text, tm_ip_addr *address);
bool tm_ip_format(uint8_t family, const tm_ip_addr *address, char *output, size_t output_size);
bool tm_ip_equal(uint8_t family, const tm_ip_addr *left, const tm_ip_addr *right);
bool tm_subnet_parse(const char *cidr, tm_subnet *subnet);
bool tm_subnet_contains(const tm_subnet *subnet, uint8_t family, const tm_ip_addr *address);
void tm_subnet_set_init(tm_subnet_set *set);
bool tm_subnet_set_add(tm_subnet_set *set, const char *cidr);
bool tm_subnet_set_contains(const tm_subnet_set *set, uint8_t family,
                            const tm_ip_addr *address);

#endif
