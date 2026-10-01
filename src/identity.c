#include "identity.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <arpa/inet.h>
#include <errno.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

static bool tm_mac_valid(const uint8_t mac[TM_MAC_LENGTH])
{
    bool nonzero = false;
    if (mac == NULL || (mac[0] & 1U) != 0) {
        return false;
    }
    for (size_t index = 0; index < TM_MAC_LENGTH; index++) {
        nonzero |= mac[index] != 0;
    }
    return nonzero;
}

static bool tm_mac_parse(const char *text, uint8_t mac[TM_MAC_LENGTH])
{
    unsigned int values[TM_MAC_LENGTH];
    char extra;
    if (text == NULL || sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &values[0], &values[1],
                               &values[2], &values[3], &values[4], &values[5], &extra) != 6) {
        return false;
    }
    for (size_t index = 0; index < TM_MAC_LENGTH; index++) {
        if (values[index] > 255U) {
            return false;
        }
        mac[index] = (uint8_t)values[index];
    }
    return tm_mac_valid(mac);
}

bool tm_identity_init(tm_identity *identity, const tm_subnet_set *local_networks,
                      const char *static_config, const char *lease_file)
{
    if (identity == NULL) {
        return false;
    }
    memset(identity, 0, sizeof(*identity));
    identity->neighbor_socket_fd = -1;
    identity->neighbors = calloc(TM_NEIGHBOR_CACHE_LIMIT, sizeof(*identity->neighbors));
    identity->unresolved = calloc(TM_UNRESOLVED_CACHE_LIMIT, sizeof(*identity->unresolved));
    identity->hostnames = calloc(TM_HOSTNAME_CACHE_LIMIT, sizeof(*identity->hostnames));
    if (identity->neighbors == NULL || identity->unresolved == NULL ||
        identity->hostnames == NULL) {
        tm_identity_destroy(identity);
        return false;
    }
    identity->local_networks = local_networks;
    identity->static_config = static_config;
    identity->lease_file = lease_file;
    return true;
}

void tm_identity_destroy(tm_identity *identity)
{
    if (identity != NULL) {
#ifdef __linux__
        if (identity->neighbor_socket_fd >= 0) {
            (void)close(identity->neighbor_socket_fd);
        }
#endif
        free(identity->neighbors);
        free(identity->unresolved);
        free(identity->hostnames);
        memset(identity, 0, sizeof(*identity));
        identity->neighbor_socket_fd = -1;
    }
}

void tm_identity_bind_aggregator(tm_identity *identity, tm_aggregator *aggregator, bool debug)
{
    if (identity != NULL) {
        identity->aggregator = aggregator;
        identity->debug = debug;
        if (aggregator != NULL) {
            time_t now = time(NULL);
            for (size_t index = 0; index < aggregator->capacity; index++) {
                const tm_aggregate_entry *entry = &aggregator->entries[index];
                if (entry->occupied && !entry->key.mac_known) {
                    (void)tm_identity_observe_unresolved(identity, entry->key.family,
                                                         &entry->key.host_ip, now);
                }
            }
        }
    }
}

bool tm_identity_add_interface(tm_identity *identity, const char *name)
{
#ifdef __linux__
    unsigned int ifindex;
    if (identity == NULL || name == NULL || identity->ifindex_count == TM_LOCAL_INTERFACE_LIMIT ||
        (ifindex = if_nametoindex(name)) == 0) {
        return false;
    }
    for (size_t index = 0; index < identity->ifindex_count; index++) {
        if (identity->ifindices[index] == ifindex) {
            return true;
        }
    }
    identity->ifindices[identity->ifindex_count++] = ifindex;
    return true;
#else
    (void)identity;
    (void)name;
    return false;
#endif
}

void tm_identity_expire(tm_identity *identity, time_t now)
{
    if (identity == NULL || identity->neighbors == NULL) {
        return;
    }
    for (size_t index = 0; index < TM_NEIGHBOR_CACHE_LIMIT; index++) {
        tm_neighbor_entry *entry = &identity->neighbors[index];
        if (entry->occupied &&
            (now < entry->seen_at || now - entry->seen_at >= TM_NEIGHBOR_TTL_SECONDS ||
             (entry->invalid_at != 0 &&
              (now < entry->invalid_at ||
               now - entry->invalid_at >= TM_NEIGHBOR_DELETE_GRACE_SECONDS)))) {
            memset(entry, 0, sizeof(*entry));
            identity->neighbor_count--;
        }
    }
    if (identity->unresolved == NULL) {
        return;
    }
    for (size_t index = 0; index < TM_UNRESOLVED_CACHE_LIMIT; index++) {
        tm_unresolved_entry *entry = &identity->unresolved[index];
        if (entry->occupied &&
            (now < entry->last_seen || now - entry->last_seen >= TM_UNRESOLVED_TTL_SECONDS)) {
            if (identity->debug) {
                char ip[46];
                if (tm_ip_format(entry->family, &entry->ip, ip, sizeof(ip))) {
                    fprintf(stderr, "trafficmon: unresolved expired ip=%s\n", ip);
                }
            }
            memset(entry, 0, sizeof(*entry));
            identity->unresolved_count--;
        }
    }
}

bool tm_identity_observe_unresolved(tm_identity *identity, uint8_t family,
                                    const tm_ip_addr *ip, time_t now)
{
    size_t free_slot = TM_UNRESOLVED_CACHE_LIMIT;
    size_t oldest_slot = TM_UNRESOLVED_CACHE_LIMIT;
    time_t oldest = now;
    if (identity == NULL || identity->unresolved == NULL || ip == NULL ||
        !tm_family_is_valid(family)) {
        return false;
    }
    tm_identity_expire(identity, now);
    for (size_t index = 0; index < TM_UNRESOLVED_CACHE_LIMIT; index++) {
        tm_unresolved_entry *entry = &identity->unresolved[index];
        if (entry->occupied && entry->family == family && tm_ip_equal(family, &entry->ip, ip)) {
            entry->last_seen = now;
            return true;
        }
        if (!entry->occupied && free_slot == TM_UNRESOLVED_CACHE_LIMIT) {
            free_slot = index;
        } else if (entry->occupied && entry->last_seen <= oldest) {
            oldest = entry->last_seen;
            oldest_slot = index;
        }
    }
    size_t slot = free_slot != TM_UNRESOLVED_CACHE_LIMIT ? free_slot : oldest_slot;
    if (slot == TM_UNRESOLVED_CACHE_LIMIT) {
        return false;
    }
    if (!identity->unresolved[slot].occupied) {
        identity->unresolved_count++;
    }
    identity->unresolved[slot] = (tm_unresolved_entry){.family = family, .ip = *ip,
        .first_seen = now, .last_seen = now, .occupied = true};
    if (identity->debug) {
        char text[46];
        if (tm_ip_format(family, ip, text, sizeof(text))) {
            fprintf(stderr, "trafficmon: flow client=%s neighbor unresolved\n", text);
        }
    }
    return true;
}

static void tm_unresolved_resolved(tm_identity *identity, uint8_t family,
                                   const tm_ip_addr *ip)
{
    if (identity == NULL || identity->unresolved == NULL) {
        return;
    }
    for (size_t index = 0; index < TM_UNRESOLVED_CACHE_LIMIT; index++) {
        tm_unresolved_entry *entry = &identity->unresolved[index];
        if (entry->occupied && entry->family == family && tm_ip_equal(family, &entry->ip, ip)) {
            memset(entry, 0, sizeof(*entry));
            identity->unresolved_count--;
            return;
        }
    }
}

static bool tm_identity_neighbor_put_full(tm_identity *identity, uint8_t family,
                                          const tm_ip_addr *ip,
                                          const uint8_t mac[TM_MAC_LENGTH],
                                          unsigned int ifindex, uint16_t nud_state, time_t now)
{
    size_t merged = 0;
    size_t slot = TM_NEIGHBOR_CACHE_LIMIT;
    size_t free_slot = TM_NEIGHBOR_CACHE_LIMIT;
    size_t oldest_slot = TM_NEIGHBOR_CACHE_LIMIT;
    time_t oldest = now;
    if (identity == NULL || identity->neighbors == NULL || ip == NULL ||
        !tm_family_is_valid(family) || !tm_mac_valid(mac)) {
        return false;
    }
    if (identity->local_networks != NULL &&
        !tm_subnet_set_contains(identity->local_networks, family, ip)) {
        return false;
    }
    tm_identity_expire(identity, now);
    for (size_t index = 0; index < TM_NEIGHBOR_CACHE_LIMIT; index++) {
        const tm_neighbor_entry *entry = &identity->neighbors[index];
        if (entry->occupied && entry->family == family && entry->ifindex == ifindex &&
            tm_ip_equal(family, &entry->ip, ip)) {
            slot = index;
            break;
        }
        if (!entry->occupied && free_slot == TM_NEIGHBOR_CACHE_LIMIT) {
            free_slot = index;
        } else if (entry->occupied && entry->seen_at <= oldest) {
            oldest = entry->seen_at;
            oldest_slot = index;
        }
    }
    if (slot == TM_NEIGHBOR_CACHE_LIMIT) {
        slot = free_slot != TM_NEIGHBOR_CACHE_LIMIT ? free_slot : oldest_slot;
    }
    if (slot == TM_NEIGHBOR_CACHE_LIMIT) {
        return false;
    }
    if (!identity->neighbors[slot].occupied) {
        identity->neighbor_count++;
    }
    identity->neighbors[slot] = (tm_neighbor_entry){.family = family, .ip = *ip,
        .ifindex = ifindex, .nud_state = nud_state, .seen_at = now, .occupied = true};
    memcpy(identity->neighbors[slot].mac, mac, TM_MAC_LENGTH);
    if (identity->aggregator != NULL &&
        !tm_aggregator_resolve_address(identity->aggregator, family, ip, mac, &merged)) {
        return false;
    }
    tm_unresolved_resolved(identity, family, ip);
    if (identity->debug) {
        char ip_text[46];
        if (tm_ip_format(family, ip, ip_text, sizeof(ip_text))) {
            fprintf(stderr,
                    "trafficmon: neighbor learned ip=%s mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    ip_text, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            if (merged != 0) {
                fprintf(stderr,
                        "trafficmon: merging unresolved ip=%s into device=%02x:%02x:%02x:%02x:%02x:%02x entries=%zu\n",
                        ip_text, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], merged);
            }
        }
    }
    return true;
}

bool tm_identity_neighbor_put(tm_identity *identity, uint8_t family, const tm_ip_addr *ip,
                              const uint8_t mac[TM_MAC_LENGTH], time_t now)
{
    return tm_identity_neighbor_put_full(identity, family, ip, mac, 0, 0, now);
}

bool tm_identity_neighbor_get(tm_identity *identity, uint8_t family, const tm_ip_addr *ip,
                              time_t now, uint8_t mac[TM_MAC_LENGTH])
{
    tm_neighbor_entry *best = NULL;
    if (identity == NULL || identity->neighbors == NULL || ip == NULL || mac == NULL) {
        return false;
    }
    for (size_t index = 0; index < TM_NEIGHBOR_CACHE_LIMIT; index++) {
        tm_neighbor_entry *entry = &identity->neighbors[index];
        if (entry->occupied && entry->family == family && tm_ip_equal(family, &entry->ip, ip)) {
            if (now < entry->seen_at || now - entry->seen_at >= TM_NEIGHBOR_TTL_SECONDS ||
                (entry->invalid_at != 0 && now >= entry->invalid_at &&
                 now - entry->invalid_at >= TM_NEIGHBOR_DELETE_GRACE_SECONDS)) {
                memset(entry, 0, sizeof(*entry));
                identity->neighbor_count--;
                continue;
            }
            if (best == NULL || entry->seen_at > best->seen_at ||
                (entry->seen_at == best->seen_at && entry->ifindex > best->ifindex)) {
                best = entry;
            }
        }
    }
    if (best == NULL) {
        return false;
    }
    memcpy(mac, best->mac, TM_MAC_LENGTH);
    return true;
}

bool tm_identity_hostname_put(tm_identity *identity, const uint8_t mac[TM_MAC_LENGTH],
                              uint8_t family, const tm_ip_addr *ip, const char *name,
                              uint8_t priority)
{
    bool known = tm_mac_valid(mac);
    size_t slot = TM_HOSTNAME_CACHE_LIMIT;
    if (identity == NULL || identity->hostnames == NULL || name == NULL || name[0] == '\0' ||
        strlen(name) >= TM_IDENTITY_NAME_LENGTH || (!known && (ip == NULL ||
                                                               !tm_family_is_valid(family)))) {
        return false;
    }
    for (size_t index = 0; index < TM_HOSTNAME_CACHE_LIMIT; index++) {
        tm_hostname_entry *entry = &identity->hostnames[index];
        if (!entry->occupied) {
            if (slot == TM_HOSTNAME_CACHE_LIMIT) {
                slot = index;
            }
        } else if (entry->mac_known == known &&
                   (known ? memcmp(entry->mac, mac, TM_MAC_LENGTH) == 0
                          : entry->family == family && tm_ip_equal(family, &entry->ip, ip))) {
            if (priority < entry->priority) {
                return true;
            }
            slot = index;
            break;
        }
    }
    if (slot == TM_HOSTNAME_CACHE_LIMIT) {
        return false;
    }
    if (!identity->hostnames[slot].occupied) {
        identity->hostname_count++;
    }
    identity->hostnames[slot] = (tm_hostname_entry){.mac_known = known, .family = family,
                                                    .priority = priority, .occupied = true};
    if (known) {
        memcpy(identity->hostnames[slot].mac, mac, TM_MAC_LENGTH);
    } else {
        identity->hostnames[slot].ip = *ip;
    }
    memcpy(identity->hostnames[slot].name, name, strlen(name) + 1);
    return true;
}

const char *tm_identity_hostname(const tm_identity *identity, bool mac_known,
                                 const uint8_t mac[TM_MAC_LENGTH], uint8_t family,
                                 const tm_ip_addr *ip)
{
    if (identity == NULL || identity->hostnames == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < TM_HOSTNAME_CACHE_LIMIT; index++) {
        const tm_hostname_entry *entry = &identity->hostnames[index];
        if (entry->occupied && entry->mac_known == mac_known &&
            (mac_known ? mac != NULL && memcmp(entry->mac, mac, TM_MAC_LENGTH) == 0
                       : ip != NULL && entry->family == family &&
                             tm_ip_equal(family, &entry->ip, ip))) {
            return entry->name;
        }
    }
    return NULL;
}

static char *tm_trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    if ((*text == '\'' || *text == '"') && end > text + 1 && end[-1] == *text) {
        text++;
        end[-1] = '\0';
    }
    return text;
}

static void tm_load_static(tm_identity *identity)
{
    FILE *input;
    char line[512];
    char mac_text[64] = "";
    char ip_text[64] = "";
    char hostname[TM_IDENTITY_NAME_LENGTH] = "";
    bool host_section = false;
    if (identity->static_config == NULL || (input = fopen(identity->static_config, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        char key[64];
        char value[256];
        char *cursor = tm_trim(line);
        if (sscanf(cursor, "config %63s", value) == 1) {
            if (host_section && hostname[0] != '\0') {
                uint8_t mac[TM_MAC_LENGTH] = {0};
                tm_ip_addr ip;
                uint8_t family = strchr(ip_text, ':') == NULL ? TM_FAMILY_IPV4 : TM_FAMILY_IPV6;
                bool ip_valid = tm_ip_parse(family, ip_text, &ip);
                if (tm_mac_parse(mac_text, mac) || ip_valid) {
                    (void)tm_identity_hostname_put(identity, mac, family,
                                                   ip_valid ? &ip : NULL, hostname, 2);
                }
            }
            host_section = strcmp(tm_trim(value), "host") == 0;
            mac_text[0] = ip_text[0] = hostname[0] = '\0';
        } else if (host_section && sscanf(cursor, "option %63s %255[^\n]", key, value) == 2) {
            const char *normalized = tm_trim(value);
            if (strcmp(key, "mac") == 0 && strlen(normalized) < sizeof(mac_text)) {
                strcpy(mac_text, normalized);
            } else if (strcmp(key, "ip") == 0 && strlen(normalized) < sizeof(ip_text)) {
                strcpy(ip_text, normalized);
            } else if (strcmp(key, "name") == 0 && strlen(normalized) < sizeof(hostname)) {
                strcpy(hostname, normalized);
            }
        }
    }
    if (host_section && hostname[0] != '\0') {
        uint8_t mac[TM_MAC_LENGTH] = {0};
        tm_ip_addr ip;
        uint8_t family = strchr(ip_text, ':') == NULL ? TM_FAMILY_IPV4 : TM_FAMILY_IPV6;
        bool ip_valid = tm_ip_parse(family, ip_text, &ip);
        if (tm_mac_parse(mac_text, mac) || ip_valid) {
            (void)tm_identity_hostname_put(identity, mac, family,
                                           ip_valid ? &ip : NULL, hostname, 2);
        }
    }
    (void)fclose(input);
}

static void tm_load_leases(tm_identity *identity, time_t now)
{
    FILE *input;
    char line[512];
    if (identity->lease_file == NULL || (input = fopen(identity->lease_file, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        char expiry[32], mac_text[64], ip_text[64], hostname[TM_IDENTITY_NAME_LENGTH];
        uint8_t mac[TM_MAC_LENGTH] = {0};
        tm_ip_addr ip;
        uint8_t family;
        char *end;
        unsigned long long expires_at;
        if (sscanf(line, "%31s %63s %63s %63s", expiry, mac_text, ip_text, hostname) != 4 ||
            strcmp(hostname, "*") == 0) {
            continue;
        }
        errno = 0;
        expires_at = strtoull(expiry, &end, 10);
        if (errno != 0 || end == expiry || *end != '\0' ||
            (expires_at != 0 && (unsigned long long)now >= expires_at)) {
            continue;
        }
        family = strchr(ip_text, ':') == NULL ? TM_FAMILY_IPV4 : TM_FAMILY_IPV6;
        if (tm_ip_parse(family, ip_text, &ip)) {
            (void)tm_mac_parse(mac_text, mac);
            (void)tm_identity_hostname_put(identity, mac, family, &ip, hostname, 1);
        }
    }
    (void)fclose(input);
}

void tm_identity_refresh_hostnames(tm_identity *identity, time_t now)
{
    if (identity == NULL || identity->hostnames == NULL || now < identity->next_hostname_refresh) {
        return;
    }
    memset(identity->hostnames, 0, TM_HOSTNAME_CACHE_LIMIT * sizeof(*identity->hostnames));
    identity->hostname_count = 0;
    tm_load_leases(identity, now);
    tm_load_static(identity);
    identity->next_hostname_refresh = now + TM_IDENTITY_REFRESH_SECONDS;
}

#ifdef __linux__
static bool tm_interface_allowed(const tm_identity *identity, unsigned int ifindex)
{
    for (size_t index = 0; index < identity->ifindex_count; index++) {
        if (identity->ifindices[index] == ifindex) {
            return true;
        }
    }
    return false;
}

static void tm_neighbor_invalidate(tm_identity *identity, uint8_t family,
                                   const tm_ip_addr *ip, unsigned int ifindex,
                                   uint16_t state, time_t now)
{
    for (size_t index = 0; index < TM_NEIGHBOR_CACHE_LIMIT; index++) {
        tm_neighbor_entry *entry = &identity->neighbors[index];
        if (entry->occupied && entry->family == family && entry->ifindex == ifindex &&
            tm_ip_equal(family, &entry->ip, ip)) {
            entry->nud_state = state;
            entry->invalid_at = now;
        }
    }
}

bool tm_identity_consume_neighbor_message(tm_identity *identity, const void *message,
                                           size_t length, time_t now)
{
    const struct nlmsghdr *header = message;
    const struct ndmsg *neighbor;
    int remaining;
    struct rtattr *attribute;
    const void *ip_data = NULL;
    const void *mac_data = NULL;
    uint8_t family;
    tm_ip_addr ip;
    if (identity == NULL || message == NULL || length < NLMSG_LENGTH(sizeof(struct ndmsg)) ||
        header->nlmsg_len > length || header->nlmsg_len < NLMSG_LENGTH(sizeof(struct ndmsg)) ||
        (header->nlmsg_type != RTM_NEWNEIGH && header->nlmsg_type != RTM_DELNEIGH)) {
        return false;
    }
    neighbor = NLMSG_DATA(header);
    family = neighbor->ndm_family == AF_INET ? TM_FAMILY_IPV4
                                             : neighbor->ndm_family == AF_INET6 ? TM_FAMILY_IPV6 : 0;
    if (!tm_family_is_valid(family) || !tm_interface_allowed(identity, neighbor->ndm_ifindex)) {
        return false;
    }
    remaining = (int)(header->nlmsg_len - NLMSG_LENGTH(sizeof(struct ndmsg)));
    for (attribute = (struct rtattr *)((char *)neighbor + NLMSG_ALIGN(sizeof(*neighbor)));
         RTA_OK(attribute, remaining); attribute = RTA_NEXT(attribute, remaining)) {
        if (attribute->rta_type == NDA_DST && RTA_PAYLOAD(attribute) == tm_ip_size(family)) {
            ip_data = RTA_DATA(attribute);
        } else if (attribute->rta_type == NDA_LLADDR && RTA_PAYLOAD(attribute) == TM_MAC_LENGTH) {
            mac_data = RTA_DATA(attribute);
        }
    }
    if (ip_data != NULL) {
        memset(&ip, 0, sizeof(ip));
        memcpy(ip.bytes, ip_data, tm_ip_size(family));
        if (header->nlmsg_type == RTM_DELNEIGH ||
            (neighbor->ndm_state & (NUD_FAILED | NUD_INCOMPLETE)) != 0) {
            tm_neighbor_invalidate(identity, family, &ip, (unsigned int)neighbor->ndm_ifindex,
                                   neighbor->ndm_state, now);
            return true;
        }
        if (mac_data == NULL) {
            return false;
        }
        if ((neighbor->ndm_state & (NUD_REACHABLE | NUD_STALE | NUD_DELAY | NUD_PROBE |
                                    NUD_PERMANENT | NUD_NOARP)) == 0) {
            return false;
        }
        return tm_identity_neighbor_put_full(identity, family, &ip, mac_data,
                                             (unsigned int)neighbor->ndm_ifindex,
                                             neighbor->ndm_state, now);
    }
    return false;
}

static bool tm_identity_open_neighbor_monitor(tm_identity *identity)
{
    struct sockaddr_nl address;
    int flags;
    if (identity->neighbor_socket_fd >= 0) {
        return true;
    }
    identity->neighbor_socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (identity->neighbor_socket_fd < 0) {
        return false;
    }
    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;
    address.nl_groups = RTMGRP_NEIGH;
    if (bind(identity->neighbor_socket_fd, (const struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        (flags = fcntl(identity->neighbor_socket_fd, F_GETFL, 0)) < 0 ||
        fcntl(identity->neighbor_socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(identity->neighbor_socket_fd);
        identity->neighbor_socket_fd = -1;
        return false;
    }
    return true;
}

bool tm_identity_tick(tm_identity *identity, time_t now)
{
    union { struct nlmsghdr header; uint8_t bytes[32768]; } buffer;
    if (identity == NULL || identity->neighbors == NULL) {
        return false;
    }
    tm_identity_expire(identity, now);
    if (identity->ifindex_count == 0) {
        return true;
    }
    if (!tm_identity_open_neighbor_monitor(identity)) {
        return false;
    }
    for (;;) {
        ssize_t received = recv(identity->neighbor_socket_fd, buffer.bytes,
                                sizeof(buffer.bytes), 0);
        int remaining;
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        remaining = (int)received;
        for (struct nlmsghdr *header = (struct nlmsghdr *)buffer.bytes;
             NLMSG_OK(header, (unsigned int)remaining);
             header = NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_type == RTM_NEWNEIGH || header->nlmsg_type == RTM_DELNEIGH) {
                (void)tm_identity_consume_neighbor_message(identity, header,
                                                           header->nlmsg_len, now);
            }
        }
    }
}

bool tm_identity_refresh_neighbors(tm_identity *identity, time_t now)
{
    int socket_fd;
    struct sockaddr_nl kernel;
    struct {
        struct nlmsghdr header;
        struct ndmsg message;
    } request;
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
    bool complete = false;
    if (identity == NULL || identity->neighbors == NULL || now < identity->next_neighbor_refresh) {
        return true;
    }
    if (!tm_identity_open_neighbor_monitor(identity)) {
        return false;
    }
    identity->next_neighbor_refresh = now + TM_NEIGHBOR_REFRESH_SECONDS;
    tm_identity_expire(identity, now);
    if (identity->ifindex_count == 0) {
        return true;
    }
    socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (socket_fd < 0) {
        return false;
    }
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    memset(&kernel, 0, sizeof(kernel));
    kernel.nl_family = AF_NETLINK;
    memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.message));
    request.header.nlmsg_type = RTM_GETNEIGH;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = 1;
    request.message.ndm_family = AF_UNSPEC;
    if (sendto(socket_fd, &request, request.header.nlmsg_len, 0,
               (const struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
        (void)close(socket_fd);
        return false;
    }
    for (unsigned int batch = 0; batch < 64 && !complete; batch++) {
        union { struct nlmsghdr header; uint8_t bytes[32768]; } buffer;
        ssize_t received = recv(socket_fd, buffer.bytes, sizeof(buffer.bytes), 0);
        int remaining;
        if (received < 0) {
            break;
        }
        remaining = (int)received;
        for (struct nlmsghdr *header = (struct nlmsghdr *)buffer.bytes;
             NLMSG_OK(header, (unsigned int)remaining);
             header = NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_seq != 1) {
                continue;
            }
            if (header->nlmsg_type == NLMSG_DONE) {
                complete = true;
                break;
            }
            if (header->nlmsg_type == NLMSG_ERROR) {
                break;
            }
            (void)tm_identity_consume_neighbor_message(identity, header, header->nlmsg_len, now);
        }
    }
    (void)close(socket_fd);
    return complete;
}
#else
bool tm_identity_refresh_neighbors(tm_identity *identity, time_t now)
{
    (void)identity;
    (void)now;
    return true;
}

bool tm_identity_tick(tm_identity *identity, time_t now)
{
    tm_identity_expire(identity, now);
    return true;
}

bool tm_identity_consume_neighbor_message(tm_identity *identity, const void *message,
                                           size_t length, time_t now)
{
    (void)identity;
    (void)message;
    (void)length;
    (void)now;
    return false;
}
#endif
