#include "csv_writer.h"

#include "subnet.h"

#include <inttypes.h>
#include <stdio.h>

static bool tm_csv_field(tm_text_buffer *output, const char *text)
{
    bool quote = false;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == ',' || *cursor == '"' || *cursor == '\r' || *cursor == '\n') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return tm_text_buffer_append(output, text);
    }
    if (!tm_text_buffer_append(output, "\"")) {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '"' && !tm_text_buffer_append(output, "\"")) {
            return false;
        }
        if (!tm_text_buffer_append_n(output, cursor, 1)) {
            return false;
        }
    }
    return tm_text_buffer_append(output, "\"");
}

bool tm_csv_write_query(const tm_ipc_query_response *response, tm_text_buffer *output)
{
    if (response == NULL || output == NULL || !tm_text_buffer_init(output)) {
        return false;
    }
    if (!tm_text_buffer_append(
            output,
            "family,ip,mac,protocol,connections,download_bytes,download_packets,upload_bytes,upload_packets\r\n")) {
        tm_text_buffer_destroy(output);
        return false;
    }
    for (size_t index = 0; index < response->count; index++) {
        const tm_ipc_query_row *row = &response->rows[index];
        char ip[46] = "";
        char mac[18] = "";
        if (tm_family_is_valid(row->row.key.family) &&
            !tm_ip_format(row->row.key.family, &row->row.key.host_ip, ip, sizeof(ip))) {
            tm_text_buffer_destroy(output);
            return false;
        }
        if (row->row.key.mac_known) {
            (void)snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                           row->row.key.mac[0], row->row.key.mac[1], row->row.key.mac[2],
                           row->row.key.mac[3], row->row.key.mac[4], row->row.key.mac[5]);
        }
        if (!tm_text_buffer_printf(output, "%u,", row->row.key.family) ||
            !tm_csv_field(output, ip) || !tm_text_buffer_append(output, ",") ||
            !tm_csv_field(output, mac) || !tm_text_buffer_append(output, ",") ||
            !tm_csv_field(output, row->protocol_name) ||
            !tm_text_buffer_printf(output,
                                   ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                                   ",%" PRIu64 "\r\n",
                                   row->row.connections, row->row.rx_bytes, row->row.rx_packets,
                                   row->row.tx_bytes, row->row.tx_packets)) {
            tm_text_buffer_destroy(output);
            return false;
        }
    }
    return true;
}
