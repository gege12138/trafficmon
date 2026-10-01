#include "aggregator.h"

#include "subnet.h"

#include <stdlib.h>
#include <string.h>

enum { TM_AGGREGATOR_INITIAL_CAPACITY = 16 };

static uint64_t tm_saturating_add(uint64_t left, uint64_t right)
{
    if (UINT64_MAX - left < right) {
        return UINT64_MAX;
    }
    return left + right;
}

static uint64_t tm_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length)
{
    size_t index;
    for (index = 0; index < length; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t tm_key_hash(const tm_aggregate_key *key)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t ip_length = tm_ip_size(key->family);
    uint8_t protocol_bytes[2] = {(uint8_t)(key->protocol_id >> 8),
                                 (uint8_t)key->protocol_id};
    hash = tm_hash_bytes(hash, &key->family, 1);
    hash = tm_hash_bytes(hash, key->host_ip.bytes, ip_length);
    hash = tm_hash_bytes(hash, key->mac, TM_MAC_LENGTH);
    hash = tm_hash_bytes(hash, (const uint8_t *)&key->mac_known, sizeof(key->mac_known));
    return tm_hash_bytes(hash, protocol_bytes, sizeof(protocol_bytes));
}

static bool tm_key_equal(const tm_aggregate_key *left, const tm_aggregate_key *right)
{
    return left->family == right->family && left->protocol_id == right->protocol_id &&
           left->mac_known == right->mac_known &&
           tm_ip_equal(left->family, &left->host_ip, &right->host_ip) &&
           memcmp(left->mac, right->mac, TM_MAC_LENGTH) == 0;
}

static size_t tm_capacity_for_limit(size_t limit)
{
    size_t capacity = TM_AGGREGATOR_INITIAL_CAPACITY;
    if (limit == 0) {
        return capacity;
    }
    while (capacity < limit && capacity <= SIZE_MAX / 2) {
        capacity *= 2;
    }
    if (capacity <= SIZE_MAX / 2) {
        capacity *= 2;
    }
    return capacity;
}

bool tm_aggregator_init(tm_aggregator *aggregator, size_t limit)
{
    size_t capacity;
    if (aggregator == NULL) {
        return false;
    }
    memset(aggregator, 0, sizeof(*aggregator));
    capacity = tm_capacity_for_limit(limit);
    aggregator->entries = calloc(capacity, sizeof(*aggregator->entries));
    if (aggregator->entries == NULL) {
        return false;
    }
    aggregator->capacity = capacity;
    aggregator->limit = limit;
    return true;
}

void tm_aggregator_destroy(tm_aggregator *aggregator)
{
    if (aggregator != NULL) {
        free(aggregator->entries);
        memset(aggregator, 0, sizeof(*aggregator));
    }
}

static tm_aggregate_entry *tm_find_slot(tm_aggregate_entry *entries, size_t capacity,
                                        const tm_aggregate_key *key)
{
    size_t index = (size_t)(tm_key_hash(key) & (uint64_t)(capacity - 1));
    size_t attempts;
    for (attempts = 0; attempts < capacity; attempts++) {
        tm_aggregate_entry *entry = &entries[index];
        if (!entry->occupied || tm_key_equal(&entry->key, key)) {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
    return NULL;
}

static bool tm_aggregator_resize(tm_aggregator *aggregator)
{
    size_t new_capacity;
    tm_aggregate_entry *new_entries;
    size_t index;
    if (aggregator->capacity > SIZE_MAX / 2) {
        return false;
    }
    new_capacity = aggregator->capacity * 2;
    new_entries = calloc(new_capacity, sizeof(*new_entries));
    if (new_entries == NULL) {
        return false;
    }
    for (index = 0; index < aggregator->capacity; index++) {
        if (aggregator->entries[index].occupied) {
            tm_aggregate_entry *slot =
                tm_find_slot(new_entries, new_capacity, &aggregator->entries[index].key);
            if (slot == NULL) {
                free(new_entries);
                return false;
            }
            *slot = aggregator->entries[index];
        }
    }
    free(aggregator->entries);
    aggregator->entries = new_entries;
    aggregator->capacity = new_capacity;
    return true;
}

static tm_aggregate_key tm_key_from_delta(const tm_flow_delta *delta, uint16_t protocol_id)
{
    tm_aggregate_key key;
    memset(&key, 0, sizeof(key));
    key.family = delta->family;
    key.host_ip = delta->host_ip;
    key.mac_known = delta->mac_known;
    if (delta->mac_known) {
        memcpy(key.mac, delta->mac, sizeof(key.mac));
    }
    key.protocol_id = protocol_id;
    return key;
}

bool tm_aggregator_add(tm_aggregator *aggregator, const tm_flow_delta *delta,
                       uint16_t protocol_id)
{
    tm_aggregate_key key;
    tm_aggregate_entry *entry;
    bool is_new;
    if (aggregator == NULL || delta == NULL || aggregator->entries == NULL ||
        !tm_family_is_valid(delta->family)) {
        return false;
    }
    key = tm_key_from_delta(delta, protocol_id);
    entry = tm_find_slot(aggregator->entries, aggregator->capacity, &key);
    if (entry == NULL) {
        return false;
    }
    is_new = !entry->occupied;
    if (is_new && aggregator->limit != 0 && aggregator->count >= aggregator->limit) {
        aggregator->dropped_new_keys = tm_saturating_add(aggregator->dropped_new_keys, 1);
        return false;
    }
    if (is_new && aggregator->limit == 0 &&
        (aggregator->count + 1) * 10 >= aggregator->capacity * 7) {
        if (!tm_aggregator_resize(aggregator)) {
            return false;
        }
        entry = tm_find_slot(aggregator->entries, aggregator->capacity, &key);
        if (entry == NULL) {
            return false;
        }
    }
    if (is_new) {
        memset(entry, 0, sizeof(*entry));
        entry->occupied = true;
        entry->key = key;
        aggregator->count++;
    }
    if (delta->new_connection) {
        entry->connections = tm_saturating_add(entry->connections, 1);
    }
    entry->rx_bytes = tm_saturating_add(entry->rx_bytes, delta->rx_bytes);
    entry->rx_packets = tm_saturating_add(entry->rx_packets, delta->rx_packets);
    entry->tx_bytes = tm_saturating_add(entry->tx_bytes, delta->tx_bytes);
    entry->tx_packets = tm_saturating_add(entry->tx_packets, delta->tx_packets);
    return true;
}

bool tm_aggregator_restore_entry(tm_aggregator *aggregator, const tm_aggregate_entry *source)
{
    tm_aggregate_entry *entry;
    if (aggregator == NULL || source == NULL || aggregator->entries == NULL ||
        !source->occupied || !tm_family_is_valid(source->key.family) ||
        source->key.protocol_id >= 256) {
        return false;
    }
    entry = tm_find_slot(aggregator->entries, aggregator->capacity, &source->key);
    if (entry == NULL || entry->occupied ||
        (aggregator->limit != 0 && aggregator->count >= aggregator->limit)) {
        return false;
    }
    if (aggregator->limit == 0 && (aggregator->count + 1) * 10 >= aggregator->capacity * 7) {
        if (!tm_aggregator_resize(aggregator)) {
            return false;
        }
        entry = tm_find_slot(aggregator->entries, aggregator->capacity, &source->key);
        if (entry == NULL || entry->occupied) {
            return false;
        }
    }
    *entry = *source;
    if (!entry->key.mac_known) {
        memset(entry->key.mac, 0, sizeof(entry->key.mac));
    }
    entry->occupied = true;
    aggregator->count++;
    return true;
}

const tm_aggregate_entry *tm_aggregator_find(const tm_aggregator *aggregator,
                                             const tm_aggregate_key *key)
{
    tm_aggregate_entry *entry;
    if (aggregator == NULL || key == NULL || aggregator->entries == NULL) {
        return NULL;
    }
    entry = tm_find_slot(aggregator->entries, aggregator->capacity, key);
    return entry != NULL && entry->occupied ? entry : NULL;
}

static bool tm_restore_or_merge(tm_aggregator *aggregator, const tm_aggregate_entry *source)
{
    tm_aggregate_entry *entry = tm_find_slot(aggregator->entries, aggregator->capacity,
                                              &source->key);
    if (entry == NULL) {
        return false;
    }
    if (!entry->occupied) {
        if (aggregator->limit != 0 && aggregator->count >= aggregator->limit) {
            return false;
        }
        *entry = *source;
        entry->occupied = true;
        aggregator->count++;
        return true;
    }
    entry->connections = tm_saturating_add(entry->connections, source->connections);
    entry->rx_bytes = tm_saturating_add(entry->rx_bytes, source->rx_bytes);
    entry->rx_packets = tm_saturating_add(entry->rx_packets, source->rx_packets);
    entry->tx_bytes = tm_saturating_add(entry->tx_bytes, source->tx_bytes);
    entry->tx_packets = tm_saturating_add(entry->tx_packets, source->tx_packets);
    return true;
}

bool tm_aggregator_resolve_address(tm_aggregator *aggregator, uint8_t family,
                                   const tm_ip_addr *ip,
                                   const uint8_t mac[TM_MAC_LENGTH], size_t *merged)
{
    tm_aggregator replacement;
    size_t changed = 0;
    bool has_match = false;
    if (aggregator == NULL || aggregator->entries == NULL || ip == NULL || mac == NULL ||
        !tm_family_is_valid(family)) {
        return false;
    }
    for (size_t index = 0; index < aggregator->capacity; index++) {
        const tm_aggregate_entry *entry = &aggregator->entries[index];
        if (entry->occupied && !entry->key.mac_known && entry->key.family == family &&
            tm_ip_equal(family, &entry->key.host_ip, ip)) {
            has_match = true;
            break;
        }
    }
    if (!has_match) {
        if (merged != NULL) {
            *merged = 0;
        }
        return true;
    }
    if (!tm_aggregator_init(&replacement, aggregator->limit)) {
        return false;
    }
    replacement.dropped_new_keys = aggregator->dropped_new_keys;
    for (size_t index = 0; index < aggregator->capacity; index++) {
        tm_aggregate_entry entry;
        if (!aggregator->entries[index].occupied) {
            continue;
        }
        entry = aggregator->entries[index];
        if (!entry.key.mac_known && entry.key.family == family &&
            tm_ip_equal(family, &entry.key.host_ip, ip)) {
            entry.key.mac_known = true;
            memcpy(entry.key.mac, mac, TM_MAC_LENGTH);
            changed++;
        }
        if (!tm_restore_or_merge(&replacement, &entry)) {
            tm_aggregator_destroy(&replacement);
            return false;
        }
    }
    tm_aggregator_destroy(aggregator);
    *aggregator = replacement;
    if (merged != NULL) {
        *merged = changed;
    }
    return true;
}
