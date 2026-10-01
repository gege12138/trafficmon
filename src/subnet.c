#include "subnet.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

static int tm_native_family(uint8_t family)
{
    if (family == TM_FAMILY_IPV4) {
        return AF_INET;
    }
    if (family == TM_FAMILY_IPV6) {
        return AF_INET6;
    }
    return AF_UNSPEC;
}

bool tm_ip_parse(uint8_t family, const char *text, tm_ip_addr *address)
{
    int native_family = tm_native_family(family);
    if (text == NULL || address == NULL || native_family == AF_UNSPEC) {
        return false;
    }

    memset(address, 0, sizeof(*address));
    return inet_pton(native_family, text, address->bytes) == 1;
}

bool tm_ip_format(uint8_t family, const tm_ip_addr *address, char *output, size_t output_size)
{
    int native_family = tm_native_family(family);
    return address != NULL && output != NULL && native_family != AF_UNSPEC &&
           inet_ntop(native_family, address->bytes, output, output_size) != NULL;
}

bool tm_ip_equal(uint8_t family, const tm_ip_addr *left, const tm_ip_addr *right)
{
    size_t length = tm_ip_size(family);
    return left != NULL && right != NULL && length != 0 &&
           memcmp(left->bytes, right->bytes, length) == 0;
}

bool tm_subnet_parse(const char *cidr, tm_subnet *subnet)
{
    char address_text[INET6_ADDRSTRLEN];
    const char *slash;
    char *end = NULL;
    unsigned long prefix;
    size_t address_length;
    uint8_t family;
    unsigned int maximum_prefix;

    if (cidr == NULL || subnet == NULL) {
        return false;
    }
    slash = strchr(cidr, '/');
    if (slash == NULL || strchr(slash + 1, '/') != NULL) {
        return false;
    }
    address_length = (size_t)(slash - cidr);
    if (address_length == 0 || address_length >= sizeof(address_text)) {
        return false;
    }
    memcpy(address_text, cidr, address_length);
    address_text[address_length] = '\0';

    family = strchr(address_text, ':') == NULL ? TM_FAMILY_IPV4 : TM_FAMILY_IPV6;
    maximum_prefix = family == TM_FAMILY_IPV4 ? 32U : 128U;
    errno = 0;
    prefix = strtoul(slash + 1, &end, 10);
    if (errno != 0 || end == slash + 1 || *end != '\0' || prefix > maximum_prefix) {
        return false;
    }

    memset(subnet, 0, sizeof(*subnet));
    subnet->family = family;
    subnet->prefix_length = (uint8_t)prefix;
    if (!tm_ip_parse(family, address_text, &subnet->network)) {
        return false;
    }
    /* Normalize an interface address/prefix to the canonical network. */
    for (size_t index = 0; index < tm_ip_size(family); index++) {
        unsigned int bit_start = (unsigned int)index * 8U;
        if (prefix <= bit_start) {
            subnet->network.bytes[index] = 0;
        } else if (prefix < bit_start + 8U) {
            subnet->network.bytes[index] &= (uint8_t)(0xffU << (bit_start + 8U - prefix));
        }
    }
    return true;
}

bool tm_subnet_contains(const tm_subnet *subnet, uint8_t family, const tm_ip_addr *address)
{
    size_t full_bytes;
    uint8_t remaining_bits;
    uint8_t mask;

    if (subnet == NULL || address == NULL || subnet->family != family) {
        return false;
    }
    full_bytes = subnet->prefix_length / 8U;
    remaining_bits = (uint8_t)(subnet->prefix_length % 8U);
    if (full_bytes != 0 && memcmp(subnet->network.bytes, address->bytes, full_bytes) != 0) {
        return false;
    }
    if (remaining_bits == 0) {
        return true;
    }
    mask = (uint8_t)(0xffU << (8U - remaining_bits));
    return (subnet->network.bytes[full_bytes] & mask) == (address->bytes[full_bytes] & mask);
}

void tm_subnet_set_init(tm_subnet_set *set)
{
    if (set != NULL) {
        memset(set, 0, sizeof(*set));
    }
}

bool tm_subnet_set_add(tm_subnet_set *set, const char *cidr)
{
    if (set == NULL || set->count >= TM_SUBNET_SET_MAX ||
        !tm_subnet_parse(cidr, &set->items[set->count])) {
        return false;
    }
    set->count++;
    return true;
}

bool tm_subnet_set_contains(const tm_subnet_set *set, uint8_t family,
                            const tm_ip_addr *address)
{
    size_t index;
    if (set == NULL) {
        return false;
    }
    for (index = 0; index < set->count; index++) {
        if (tm_subnet_contains(&set->items[index], family, address)) {
            return true;
        }
    }
    return false;
}
