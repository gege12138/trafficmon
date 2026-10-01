#include "query.h"

#include "subnet.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int (*tm_row_compare)(const tm_query_row *, const tm_query_row *, const void *);

static uint64_t tm_saturating_add(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static int tm_compare_bytes(const uint8_t *left, const uint8_t *right, size_t length)
{
    int comparison = memcmp(left, right, length);
    return comparison < 0 ? -1 : comparison > 0 ? 1 : 0;
}

static int tm_compare_group_key(const tm_query_row *left, const tm_query_row *right,
                                const void *context)
{
    uint32_t group_by = *(const uint32_t *)context;
    int comparison;
    if ((group_by & TM_GROUP_DEVICE) != 0) {
        if (left->key.mac_known != right->key.mac_known) {
            return left->key.mac_known ? 1 : -1;
        }
        if (left->key.mac_known) {
            comparison = tm_compare_bytes(left->key.mac, right->key.mac, TM_MAC_LENGTH);
            if (comparison != 0) {
                return comparison;
            }
        } else {
            if (left->key.family != right->key.family) {
                return left->key.family < right->key.family ? -1 : 1;
            }
            comparison = tm_compare_bytes(left->key.host_ip.bytes, right->key.host_ip.bytes,
                                          tm_ip_size(left->key.family));
            if (comparison != 0) {
                return comparison;
            }
        }
    }
    if ((group_by & (TM_GROUP_FAMILY | TM_GROUP_IP)) != 0 &&
        left->key.family != right->key.family) {
        return left->key.family < right->key.family ? -1 : 1;
    }
    if ((group_by & TM_GROUP_IP) != 0) {
        comparison = tm_compare_bytes(left->key.host_ip.bytes, right->key.host_ip.bytes,
                                      tm_ip_size(left->key.family));
        if (comparison != 0) {
            return comparison;
        }
    }
    if ((group_by & TM_GROUP_MAC) != 0) {
        if (left->key.mac_known != right->key.mac_known) {
            return left->key.mac_known ? 1 : -1;
        }
        comparison = tm_compare_bytes(left->key.mac, right->key.mac, TM_MAC_LENGTH);
        if (comparison != 0) {
            return comparison;
        }
    }
    if ((group_by & TM_GROUP_PROTOCOL) != 0 &&
        left->key.protocol_id != right->key.protocol_id) {
        return left->key.protocol_id < right->key.protocol_id ? -1 : 1;
    }
    return 0;
}

static uint64_t tm_order_value(const tm_query_row *row, tm_query_order order)
{
    switch (order) {
    case TM_ORDER_RX_BYTES:
        return row->rx_bytes;
    case TM_ORDER_TX_BYTES:
        return row->tx_bytes;
    case TM_ORDER_CONNECTIONS:
        return row->connections;
    case TM_ORDER_TOTAL_BYTES:
    default:
        return tm_saturating_add(row->rx_bytes, row->tx_bytes);
    }
}

static int tm_compare_order(const tm_query_row *left, const tm_query_row *right,
                            const void *context)
{
    const tm_query_spec *spec = context;
    uint64_t left_value = tm_order_value(left, spec->order);
    uint64_t right_value = tm_order_value(right, spec->order);
    uint32_t all_groups = TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC |
                          TM_GROUP_PROTOCOL | TM_GROUP_DEVICE;
    int comparison;
    if (left_value != right_value) {
        comparison = left_value < right_value ? -1 : 1;
        return spec->descending ? -comparison : comparison;
    }
    return tm_compare_group_key(left, right, &all_groups);
}

static bool tm_merge_sort(tm_query_row *rows, size_t count, tm_row_compare compare,
                          const void *context)
{
    tm_query_row *scratch;
    size_t width;
    if (count < 2) {
        return true;
    }
    scratch = malloc(count * sizeof(*scratch));
    if (scratch == NULL) {
        return false;
    }
    for (width = 1; width < count; width *= 2) {
        size_t start;
        for (start = 0; start < count; start += width * 2) {
            size_t middle = start + width < count ? start + width : count;
            size_t end = start + width * 2 < count ? start + width * 2 : count;
            size_t left = start;
            size_t right = middle;
            size_t output = start;
            while (left < middle || right < end) {
                if (right >= end ||
                    (left < middle && compare(&rows[left], &rows[right], context) <= 0)) {
                    scratch[output++] = rows[left++];
                } else {
                    scratch[output++] = rows[right++];
                }
            }
        }
        memcpy(rows, scratch, count * sizeof(*rows));
        if (width > count / 2) {
            break;
        }
    }
    free(scratch);
    return true;
}

static tm_query_row tm_row_from_entry(const tm_aggregate_entry *entry, uint32_t group_by,
                                      tm_identity *identity)
{
    tm_query_row row;
    memset(&row, 0, sizeof(row));
    if ((group_by & TM_GROUP_DEVICE) != 0) {
        row.key.mac_known = entry->key.mac_known;
        if (row.key.mac_known) {
            memcpy(row.key.mac, entry->key.mac, TM_MAC_LENGTH);
        } else if (identity != NULL &&
                   tm_identity_neighbor_get(identity, entry->key.family, &entry->key.host_ip,
                                            time(NULL), row.key.mac)) {
            row.key.mac_known = true;
        } else {
            row.key.family = entry->key.family;
            row.key.host_ip = entry->key.host_ip;
        }
    }
    if ((group_by & TM_GROUP_FAMILY) != 0) {
        row.key.family = entry->key.family;
    }
    if ((group_by & TM_GROUP_IP) != 0) {
        row.key.family = entry->key.family;
        row.key.host_ip = entry->key.host_ip;
    }
    if ((group_by & TM_GROUP_MAC) != 0) {
        memcpy(row.key.mac, entry->key.mac, sizeof(row.key.mac));
        row.key.mac_known = entry->key.mac_known;
    }
    if ((group_by & TM_GROUP_PROTOCOL) != 0) {
        row.key.protocol_id = entry->key.protocol_id;
    }
    row.connections = entry->connections;
    row.rx_bytes = entry->rx_bytes;
    row.rx_packets = entry->rx_packets;
    row.tx_bytes = entry->tx_bytes;
    row.tx_packets = entry->tx_packets;
    return row;
}

static void tm_row_merge(tm_query_row *target, const tm_query_row *source)
{
    target->connections = tm_saturating_add(target->connections, source->connections);
    target->rx_bytes = tm_saturating_add(target->rx_bytes, source->rx_bytes);
    target->rx_packets = tm_saturating_add(target->rx_packets, source->rx_packets);
    target->tx_bytes = tm_saturating_add(target->tx_bytes, source->tx_bytes);
    target->tx_packets = tm_saturating_add(target->tx_packets, source->tx_packets);
}

bool tm_query_run_filtered(const tm_aggregator *aggregator, const tm_query_spec *spec,
                           tm_identity *identity, tm_query_predicate predicate, void *context,
                           tm_query_result *result)
{
    tm_query_row *all_rows;
    size_t populated = 0;
    size_t grouped = 0;
    size_t index;
    size_t available;
    size_t selected;

    if (aggregator == NULL || spec == NULL || result == NULL ||
        (spec->group_by & ~(TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC |
                            TM_GROUP_PROTOCOL | TM_GROUP_DEVICE)) != 0 ||
        ((spec->group_by & TM_GROUP_DEVICE) != 0 &&
         (spec->group_by & (TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC)) != 0) ||
        spec->order > TM_ORDER_CONNECTIONS) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    if (aggregator->count == 0) {
        return true;
    }
    all_rows = malloc(aggregator->count * sizeof(*all_rows));
    if (all_rows == NULL) {
        return false;
    }
    for (index = 0; index < aggregator->capacity; index++) {
        if (aggregator->entries[index].occupied) {
            all_rows[populated++] = tm_row_from_entry(&aggregator->entries[index],
                                                       spec->group_by, identity);
        }
    }
    if (!tm_merge_sort(all_rows, populated, tm_compare_group_key, &spec->group_by)) {
        free(all_rows);
        return false;
    }
    for (index = 0; index < populated; index++) {
        if (grouped != 0 &&
            tm_compare_group_key(&all_rows[grouped - 1], &all_rows[index], &spec->group_by) == 0) {
            tm_row_merge(&all_rows[grouped - 1], &all_rows[index]);
        } else {
            all_rows[grouped++] = all_rows[index];
        }
    }
    if (predicate != NULL) {
        size_t kept = 0;
        for (index = 0; index < grouped; index++) {
            if (predicate(&all_rows[index], context)) {
                all_rows[kept++] = all_rows[index];
            }
        }
        grouped = kept;
    }
    if (!tm_merge_sort(all_rows, grouped, tm_compare_order, spec)) {
        free(all_rows);
        return false;
    }
    result->total_count = grouped;
    if (spec->offset >= grouped) {
        free(all_rows);
        return true;
    }
    available = grouped - spec->offset;
    selected = spec->limit == 0 || spec->limit > available ? available : spec->limit;
    result->rows = malloc(selected * sizeof(*result->rows));
    if (result->rows == NULL) {
        free(all_rows);
        return false;
    }
    memcpy(result->rows, &all_rows[spec->offset], selected * sizeof(*result->rows));
    result->count = selected;
    free(all_rows);
    return true;
}

bool tm_query_run_with_identity(const tm_aggregator *aggregator, const tm_query_spec *spec,
                                tm_identity *identity, tm_query_result *result)
{
    return tm_query_run_filtered(aggregator, spec, identity, NULL, NULL, result);
}

bool tm_query_run(const tm_aggregator *aggregator, const tm_query_spec *spec,
                  tm_query_result *result)
{
    return tm_query_run_with_identity(aggregator, spec, NULL, result);
}

void tm_query_result_destroy(tm_query_result *result)
{
    if (result != NULL) {
        free(result->rows);
        memset(result, 0, sizeof(*result));
    }
}
