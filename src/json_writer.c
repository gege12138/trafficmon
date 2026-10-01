#include "json_writer.h"

#include "period.h"
#include "subnet.h"

#include <inttypes.h>
#include <stdio.h>

static bool tm_json_string(tm_text_buffer *output, const char *text)
{
    if (!tm_text_buffer_append(output, "\"")) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        char escaped[7];
        if (*cursor == '"' || *cursor == '\\') {
            char pair[2] = {'\\', (char)*cursor};
            if (!tm_text_buffer_append_n(output, pair, sizeof(pair))) {
                return false;
            }
        } else if (*cursor == '\b' || *cursor == '\f' || *cursor == '\n' || *cursor == '\r' ||
                   *cursor == '\t') {
            char code = *cursor == '\b' ? 'b' : *cursor == '\f' ? 'f' : *cursor == '\n' ? 'n'
                                                       : *cursor == '\r'   ? 'r'
                                                                            : 't';
            char pair[2] = {'\\', code};
            if (!tm_text_buffer_append_n(output, pair, sizeof(pair))) {
                return false;
            }
        } else if (*cursor < 0x20 || *cursor >= 0x80) {
            (void)snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
            if (!tm_text_buffer_append(output, escaped)) {
                return false;
            }
        } else if (!tm_text_buffer_append_n(output, (const char *)cursor, 1)) {
            return false;
        }
    }
    return tm_text_buffer_append(output, "\"");
}

static bool tm_format_mac(const tm_ipc_query_row *row, char output[18])
{
    if (!row->row.key.mac_known) {
        return false;
    }
    return snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x", row->row.key.mac[0],
                    row->row.key.mac[1], row->row.key.mac[2], row->row.key.mac[3],
                    row->row.key.mac[4], row->row.key.mac[5]) == 17;
}

bool tm_json_write_query(const tm_ipc_query_response *response, tm_text_buffer *output)
{
    tm_date period_start;
    char period_text[11];
    if (response == NULL || output == NULL ||
        !tm_date_decode(response->period_start, &period_start) ||
        !tm_date_format(period_start, period_text)) {
        return false;
    }
    if (!tm_text_buffer_init(output)) {
        return false;
    }
    if (!tm_text_buffer_append(output, "{\"period\":")) {
        tm_text_buffer_destroy(output);
        return false;
    }
    if (!tm_json_string(output, period_text) || !tm_text_buffer_append(output, ",\"records\":[")) {
        tm_text_buffer_destroy(output);
        return false;
    }
    for (size_t index = 0; index < response->count; index++) {
        const tm_ipc_query_row *row = &response->rows[index];
        char ip[46] = "";
        char mac[18];
        if (index != 0 && !tm_text_buffer_append(output, ",")) {
            tm_text_buffer_destroy(output);
            return false;
        }
        if (tm_family_is_valid(row->row.key.family) &&
            !tm_ip_format(row->row.key.family, &row->row.key.host_ip, ip, sizeof(ip))) {
            tm_text_buffer_destroy(output);
            return false;
        }
        if (!tm_text_buffer_printf(output, "{\"family\":%u,\"ip\":", row->row.key.family) ||
            !tm_json_string(output, ip) || !tm_text_buffer_append(output, ",\"mac\":")) {
            tm_text_buffer_destroy(output);
            return false;
        }
        if (tm_format_mac(row, mac)) {
            if (!tm_json_string(output, mac)) {
                tm_text_buffer_destroy(output);
                return false;
            }
        } else if (!tm_text_buffer_append(output, "null")) {
            tm_text_buffer_destroy(output);
            return false;
        }
        if (!tm_text_buffer_append(output, ",\"protocol\":") ||
            !tm_json_string(output, row->protocol_name) ||
            !tm_text_buffer_printf(
                output,
                ",\"connections\":%" PRIu64 ",\"download_bytes\":%" PRIu64
                ",\"download_packets\":%" PRIu64 ",\"upload_bytes\":%" PRIu64
                ",\"upload_packets\":%" PRIu64 "}",
                row->row.connections, row->row.rx_bytes, row->row.rx_packets, row->row.tx_bytes,
                row->row.tx_packets)) {
            tm_text_buffer_destroy(output);
            return false;
        }
    }
    if (!tm_text_buffer_append(output, "]}\n")) {
        tm_text_buffer_destroy(output);
        return false;
    }
    return true;
}
