#include "device_query.h"

#include "subnet.h"

#include <inttypes.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TM_DEVICE_ADDRESS_LIMIT = 32 };

typedef struct {
    const tm_aggregate_entry *entry;
    tm_aggregate_key device_key;
} tm_device_source;

typedef struct {
    const char *filter;
    bool exact;
    const tm_device_source *sources;
    size_t source_count;
    tm_identity *identity;
} tm_device_filter;

static bool tm_json_string(tm_text_buffer *output, const char *text)
{
    if (!tm_text_buffer_append(output, "\"")) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            char pair[2] = {'\\', (char)*cursor};
            if (!tm_text_buffer_append_n(output, pair, 2)) {
                return false;
            }
        } else if (*cursor < 0x20 || *cursor >= 0x80) {
            if (!tm_text_buffer_printf(output, "\\u%04x", *cursor)) {
                return false;
            }
        } else if (!tm_text_buffer_append_n(output, (const char *)cursor, 1)) {
            return false;
        }
    }
    return tm_text_buffer_append(output, "\"");
}

static void tm_device_key(const tm_aggregate_entry *entry, tm_identity *identity,
                          tm_aggregate_key *key)
{
    memset(key, 0, sizeof(*key));
    key->mac_known = entry->key.mac_known;
    if (key->mac_known) {
        memcpy(key->mac, entry->key.mac, TM_MAC_LENGTH);
    } else if (identity != NULL &&
               tm_identity_neighbor_get(identity, entry->key.family, &entry->key.host_ip,
                                        time(NULL), key->mac)) {
        key->mac_known = true;
    } else {
        key->family = entry->key.family;
        key->host_ip = entry->key.host_ip;
    }
    key->protocol_id = entry->key.protocol_id;
}

static bool tm_matches(const tm_device_source *source, const tm_query_row *row,
                       const tm_query_spec *spec)
{
    const tm_aggregate_key *key = &source->device_key;
    if (key->mac_known != row->key.mac_known ||
        ((spec->group_by & TM_GROUP_PROTOCOL) != 0 &&
         key->protocol_id != row->key.protocol_id)) {
        return false;
    }
    return key->mac_known ? memcmp(key->mac, row->key.mac, TM_MAC_LENGTH) == 0
                          : key->family == row->key.family &&
                                tm_ip_equal(key->family, &key->host_ip, &row->key.host_ip);
}

static bool tm_contains_ascii(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    for (; *haystack != '\0'; haystack++) {
        const char *left = haystack;
        const char *right = needle;
        while (*left != '\0' && *right != '\0' &&
               tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
            left++;
            right++;
        }
        if (*right == '\0') {
            return true;
        }
    }
    return false;
}

static bool tm_format_device_id(const tm_query_row *row, char id[52], char mac[18])
{
    char ip[46];
    mac[0] = '\0';
    if (row->key.mac_known) {
        (void)snprintf(mac, 18, "%02x:%02x:%02x:%02x:%02x:%02x", row->key.mac[0],
                       row->key.mac[1], row->key.mac[2], row->key.mac[3], row->key.mac[4],
                       row->key.mac[5]);
        strcpy(id, mac);
        return true;
    }
    if (!tm_ip_format(row->key.family, &row->key.host_ip, ip, sizeof(ip))) {
        return false;
    }
    (void)snprintf(id, 52, "ip%u:%s", row->key.family, ip);
    return true;
}

static bool tm_filter_row(const tm_query_row *row, void *context)
{
    const tm_device_filter *filter = context;
    char id[52], mac[18], ip[46];
    const char *hostname;
    /* Device listings contain durable MAC identities only. IP-only rows remain
       available in address-level accounting and the unresolved status count. */
    if (!row->key.mac_known || !tm_format_device_id(row, id, mac)) {
        return false;
    }
    if (filter->filter == NULL) {
        return true;
    }
    if (filter->exact) {
        return strcmp(id, filter->filter) == 0;
    }
    if (tm_contains_ascii(id, filter->filter)) {
        return true;
    }
    hostname = tm_identity_hostname(filter->identity, row->key.mac_known, row->key.mac,
                                    row->key.family, &row->key.host_ip);
    if (tm_contains_ascii(hostname, filter->filter)) {
        return true;
    }
    for (size_t index = 0; index < filter->source_count; index++) {
        const tm_device_source *source = &filter->sources[index];
        if (source->device_key.mac_known != row->key.mac_known ||
            (row->key.mac_known &&
             memcmp(source->device_key.mac, row->key.mac, TM_MAC_LENGTH) != 0) ||
            (!row->key.mac_known &&
             (source->device_key.family != row->key.family ||
              !tm_ip_equal(row->key.family, &source->device_key.host_ip,
                           &row->key.host_ip)))) {
            continue;
        }
        if (tm_ip_format(source->entry->key.family, &source->entry->key.host_ip, ip,
                         sizeof(ip)) && tm_contains_ascii(ip, filter->filter)) {
            return true;
        }
    }
    return false;
}

static bool tm_device_addresses(const tm_device_source *sources, size_t source_count,
                                const tm_query_row *row, const tm_query_spec *spec,
                                tm_text_buffer *output)
{
    char addresses[TM_DEVICE_ADDRESS_LIMIT][46];
    size_t count = 0;
    bool truncated = false;
    for (size_t index = 0; index < source_count; index++) {
        const tm_aggregate_entry *entry = sources[index].entry;
        char text[46];
        bool duplicate = false;
        if (!tm_matches(&sources[index], row, spec) ||
            !tm_ip_format(entry->key.family, &entry->key.host_ip, text, sizeof(text))) {
            continue;
        }
        for (size_t prior = 0; prior < count; prior++) {
            if (strcmp(addresses[prior], text) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (count == TM_DEVICE_ADDRESS_LIMIT) {
            truncated = true;
            continue;
        }
        strcpy(addresses[count++], text);
    }
    if (!tm_text_buffer_append(output, "[")) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if ((index != 0 && !tm_text_buffer_append(output, ",")) ||
            !tm_json_string(output, addresses[index])) {
            return false;
        }
    }
    return tm_text_buffer_printf(output, "],\"addresses_truncated\":%s",
                                 truncated ? "true" : "false");
}

bool tm_device_query_render(const tm_aggregator *source, const tm_query_spec *spec,
                            tm_identity *identity, bool resolve_neighbors,
                            const tm_protocol_db *protocols, const char *period, bool json,
                            const char *filter, bool exact,
                            tm_text_buffer *output)
{
    tm_query_result result;
    tm_device_source *sources = NULL;
    size_t source_count = 0;
    tm_device_filter filter_context;
    bool success = false;
    if (source == NULL || spec == NULL || output == NULL || period == NULL ||
        (spec->group_by & TM_GROUP_DEVICE) == 0) {
        return false;
    }
    if (source->count != 0) {
        sources = calloc(source->count, sizeof(*sources));
        if (sources == NULL) {
            return false;
        }
        for (size_t index = 0; index < source->capacity; index++) {
            const tm_aggregate_entry *entry = &source->entries[index];
            if (entry->occupied) {
                sources[source_count].entry = entry;
                tm_device_key(entry, resolve_neighbors ? identity : NULL,
                              &sources[source_count].device_key);
                source_count++;
            }
        }
    }
    filter_context = (tm_device_filter){filter, exact, sources, source_count, identity};
    if (!tm_query_run_filtered(source, spec, resolve_neighbors ? identity : NULL,
                               tm_filter_row, &filter_context, &result)) {
        free(sources);
        return false;
    }
    if (!tm_text_buffer_init(output)) {
        tm_query_result_destroy(&result);
        free(sources);
        return false;
    }
    if (json) {
        if (!tm_text_buffer_append(output, "{\"period\":") ||
            !tm_json_string(output, period) ||
            !tm_text_buffer_printf(output, ",\"total_count\":%zu,\"devices\":[",
                                   result.total_count)) {
            goto cleanup;
        }
    } else if (!tm_text_buffer_append(output,
                                      "Device                 MAC               Download       Upload         Total\n")) {
        goto cleanup;
    }
    for (size_t index = 0; index < result.count; index++) {
        const tm_query_row *row = &result.rows[index];
        char mac[18] = "";
        char device_id[52];
        const char *hostname;
        uint64_t total = UINT64_MAX - row->rx_bytes < row->tx_bytes
                             ? UINT64_MAX : row->rx_bytes + row->tx_bytes;
        if (!tm_format_device_id(row, device_id, mac)) {
            goto cleanup;
        }
        hostname = tm_identity_hostname(identity, row->key.mac_known, row->key.mac,
                                        row->key.family, &row->key.host_ip);
        if (hostname == NULL && row->key.mac_known && identity != NULL) {
            for (size_t entry_index = 0; entry_index < source_count; entry_index++) {
                const tm_aggregate_entry *entry = sources[entry_index].entry;
                if (tm_matches(&sources[entry_index], row, spec)) {
                    hostname = tm_identity_hostname(identity, false, NULL, entry->key.family,
                                                    &entry->key.host_ip);
                    if (hostname != NULL) {
                        break;
                    }
                }
            }
        }
        if (!json) {
            if (!tm_text_buffer_printf(output, "%-22.22s %-17s %14" PRIu64 " %14" PRIu64
                                       " %14" PRIu64 "\n",
                                       hostname != NULL ? hostname : device_id,
                                       row->key.mac_known ? mac : "unknown", row->rx_bytes,
                                       row->tx_bytes, total)) {
                goto cleanup;
            }
            continue;
        }
        if ((index != 0 && !tm_text_buffer_append(output, ",")) ||
            !tm_text_buffer_append(output, "{\"device_id\":") ||
            !tm_json_string(output, device_id) || !tm_text_buffer_append(output, ",\"mac\":")) {
            goto cleanup;
        }
        if (row->key.mac_known ? !tm_json_string(output, mac)
                               : !tm_text_buffer_append(output, "null")) {
            goto cleanup;
        }
        if (!tm_text_buffer_append(output, ",\"hostname\":")) {
            goto cleanup;
        }
        if (hostname != NULL ? !tm_json_string(output, hostname)
                             : !tm_text_buffer_append(output, "null")) {
            goto cleanup;
        }
        if (!tm_text_buffer_append(output, ",\"addresses\":") ||
            !tm_device_addresses(sources, source_count, row, spec, output) ||
            !tm_text_buffer_printf(output,
                                   ",\"protocol_id\":%u,\"protocol\":",
                                   (spec->group_by & TM_GROUP_PROTOCOL) != 0
                                       ? row->key.protocol_id : 0) ||
            !tm_json_string(output, tm_protocol_db_name(protocols,
                (spec->group_by & TM_GROUP_PROTOCOL) != 0 ? row->key.protocol_id : 0)) ||
            !tm_text_buffer_printf(output,
                                   ",\"connections\":%" PRIu64
                                   ",\"download_bytes\":%" PRIu64
                                   ",\"download_packets\":%" PRIu64
                                   ",\"upload_bytes\":%" PRIu64
                                   ",\"upload_packets\":%" PRIu64
                                   ",\"total_bytes\":%" PRIu64 "}",
                                   row->connections, row->rx_bytes, row->rx_packets,
                                   row->tx_bytes, row->tx_packets, total)) {
            goto cleanup;
        }
    }
    if (json && !tm_text_buffer_append(output, "]}\n")) {
        goto cleanup;
    }
    success = true;
cleanup:
    free(sources);
    tm_query_result_destroy(&result);
    if (!success) {
        tm_text_buffer_destroy(output);
    }
    return success;
}
