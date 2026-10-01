#include "cli.h"

#include "csv_writer.h"
#include "ipc_client.h"
#include "ipc_protocol.h"
#include "json_writer.h"
#include "period.h"
#include "subnet.h"
#include "text_buffer.h"
#include "version.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tm_cli_usage(void)
{
    fprintf(stderr,
            "usage: trafficctl [--socket PATH] status|list|show|json|csv|commit [options]\n"
            "query options: --period current|YYYY-MM-DD --group family|ip|mac|device|protocol "
            "--order total|download|upload|connections --asc|--desc --limit N --offset N "
            "[--search TEXT|--device-id ID]\n");
}

static bool tm_parse_uint(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static int tm_response_exit(const tm_ipc_client_response *response)
{
    if (response->status == TM_IPC_STATUS_OK) {
        return TM_CLI_SUCCESS;
    }
    fprintf(stderr, "trafficctl: daemon returned error %u\n", response->status);
    return response->status == TM_IPC_STATUS_DATABASE_ERROR ||
                   response->status == TM_IPC_STATUS_QUERY_ERROR
               ? TM_CLI_DATABASE_ERROR
               : TM_CLI_PROTOCOL_ERROR;
}

static int tm_simple_command(const char *socket_path, uint16_t command, const uint8_t *payload,
                             size_t payload_length, bool print_payload)
{
    tm_ipc_client_response response;
    int exit_code;
    if (!tm_ipc_client_request(socket_path, command, payload, payload_length, &response)) {
        fprintf(stderr, "trafficctl: daemon unavailable\n");
        return TM_CLI_DAEMON_UNAVAILABLE;
    }
    exit_code = tm_response_exit(&response);
    if (exit_code == TM_CLI_SUCCESS && print_payload && response.payload_length != 0) {
        (void)fwrite(response.payload, 1, response.payload_length, stdout);
    }
    tm_ipc_client_response_destroy(&response);
    return exit_code;
}

static bool tm_parse_query_options(int argc, char **argv, int start, tm_ipc_query_request *request,
                                   char filter[65], bool *exact)
{
    bool explicit_group = false;
    filter[0] = '\0';
    *exact = false;
    memset(request, 0, sizeof(*request));
    request->query.group_by = TM_GROUP_FAMILY | TM_GROUP_IP | TM_GROUP_MAC | TM_GROUP_PROTOCOL;
    request->query.order = TM_ORDER_TOTAL_BYTES;
    request->query.descending = true;
    request->query.limit = 1000;
    for (int index = start; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--asc") == 0) {
            request->query.descending = false;
        } else if (strcmp(argument, "--desc") == 0) {
            request->query.descending = true;
        } else {
            const char *value;
            if (index + 1 >= argc) {
                return false;
            }
            value = argv[++index];
            if (strcmp(argument, "--period") == 0) {
                tm_date date;
                if (strcmp(value, "current") == 0) {
                    request->period_start = 0;
                } else if (!tm_date_parse(value, &date)) {
                    return false;
                } else {
                    request->period_start = tm_date_encode(date);
                }
            } else if (strcmp(argument, "--group") == 0) {
                if (!explicit_group) {
                    request->query.group_by = 0;
                    explicit_group = true;
                }
                if (strcmp(value, "family") == 0) {
                    request->query.group_by |= TM_GROUP_FAMILY;
                } else if (strcmp(value, "ip") == 0) {
                    request->query.group_by |= TM_GROUP_FAMILY | TM_GROUP_IP;
                } else if (strcmp(value, "mac") == 0) {
                    request->query.group_by |= TM_GROUP_MAC;
                } else if (strcmp(value, "device") == 0) {
                    request->query.group_by |= TM_GROUP_DEVICE;
                } else if (strcmp(value, "protocol") == 0) {
                    request->query.group_by |= TM_GROUP_PROTOCOL;
                } else {
                    return false;
                }
            } else if (strcmp(argument, "--order") == 0) {
                if (strcmp(value, "total") == 0) {
                    request->query.order = TM_ORDER_TOTAL_BYTES;
                } else if (strcmp(value, "download") == 0) {
                    request->query.order = TM_ORDER_RX_BYTES;
                } else if (strcmp(value, "upload") == 0) {
                    request->query.order = TM_ORDER_TX_BYTES;
                } else if (strcmp(value, "connections") == 0) {
                    request->query.order = TM_ORDER_CONNECTIONS;
                } else {
                    return false;
                }
            } else if (strcmp(argument, "--limit") == 0) {
                uint32_t parsed;
                if (!tm_parse_uint(value, TM_IPC_MAX_QUERY_LIMIT, &parsed)) {
                    return false;
                }
                request->query.limit = parsed;
            } else if (strcmp(argument, "--offset") == 0) {
                uint32_t parsed;
                if (!tm_parse_uint(value, UINT32_MAX, &parsed)) {
                    return false;
                }
                request->query.offset = parsed;
            } else if (strcmp(argument, "--search") == 0 ||
                       strcmp(argument, "--device-id") == 0) {
                size_t length = strlen(value);
                if (filter[0] != '\0' || length == 0 || length >= 65) {
                    return false;
                }
                for (size_t position = 0; position < length; position++) {
                    if ((unsigned char)value[position] < 0x20 ||
                        (unsigned char)value[position] > 0x7e) {
                        return false;
                    }
                }
                strcpy(filter, value);
                *exact = strcmp(argument, "--device-id") == 0;
            } else {
                return false;
            }
        }
    }
    return true;
}

static uint64_t tm_saturating_total(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static void tm_human_bytes(uint64_t bytes, char output[32])
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    double value = (double)bytes;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) {
        (void)snprintf(output, 32, "%" PRIu64 " B", bytes);
    } else {
        (void)snprintf(output, 32, "%.2f %s", value, units[unit]);
    }
}

static bool tm_show_query(const tm_ipc_query_response *response)
{
    printf("%-39s %-12s %-12s %-12s %-12s\n", "Device", "Download", "Upload", "Total",
           "Connections");
    for (size_t index = 0; index < response->count; index++) {
        const tm_ipc_query_row *row = &response->rows[index];
        char ip[46] = "-";
        char download[32];
        char upload[32];
        char total[32];
        if (tm_family_is_valid(row->row.key.family) &&
            !tm_ip_format(row->row.key.family, &row->row.key.host_ip, ip, sizeof(ip))) {
            return false;
        }
        tm_human_bytes(row->row.rx_bytes, download);
        tm_human_bytes(row->row.tx_bytes, upload);
        tm_human_bytes(tm_saturating_total(row->row.rx_bytes, row->row.tx_bytes), total);
        printf("%-39s %-12s %-12s %-12s %-12" PRIu64 "\n", ip, download, upload, total,
               row->row.connections);
    }
    return true;
}

static int tm_query_command(const char *socket_path, const char *command, int argc, char **argv,
                            int option_start)
{
    tm_ipc_query_request request;
    uint8_t request_payload[TM_IPC_QUERY_REQUEST_SIZE + 65];
    char filter[65];
    bool exact;
    size_t request_length = TM_IPC_QUERY_REQUEST_SIZE;
    tm_ipc_client_response raw_response;
    tm_ipc_query_response query_response;
    tm_text_buffer output;
    int exit_code;
    if (!tm_parse_query_options(argc, argv, option_start, &request, filter, &exact) ||
        !tm_ipc_query_request_encode(&request, request_payload)) {
        tm_cli_usage();
        return TM_CLI_INVALID_USAGE;
    }
    if ((request.query.group_by & TM_GROUP_DEVICE) != 0) {
        uint16_t device_command = exact ? TM_IPC_COMMAND_DEVICE_DETAIL_JSON
                                  : strcmp(command, "show") == 0
                                      ? TM_IPC_COMMAND_DEVICE_SHOW : TM_IPC_COMMAND_DEVICE_JSON;
        if (strcmp(command, "csv") == 0) {
            fprintf(stderr, "trafficctl: device CSV is not supported; use json or show\n");
            return TM_CLI_INVALID_USAGE;
        }
        if (exact && strcmp(command, "json") != 0) {
            return TM_CLI_INVALID_USAGE;
        }
        if (filter[0] != '\0') {
            request_length += strlen(filter) + 1;
            memcpy(request_payload + TM_IPC_QUERY_REQUEST_SIZE, filter,
                   strlen(filter) + 1);
        }
        return tm_simple_command(socket_path, device_command, request_payload,
                                 request_length, true);
    }
    if (filter[0] != '\0') {
        return TM_CLI_INVALID_USAGE;
    }
    if (!tm_ipc_client_request(socket_path, TM_IPC_COMMAND_QUERY, request_payload,
                               TM_IPC_QUERY_REQUEST_SIZE, &raw_response)) {
        fprintf(stderr, "trafficctl: daemon unavailable\n");
        return TM_CLI_DAEMON_UNAVAILABLE;
    }
    exit_code = tm_response_exit(&raw_response);
    if (exit_code != TM_CLI_SUCCESS) {
        tm_ipc_client_response_destroy(&raw_response);
        return exit_code;
    }
    if (!tm_ipc_query_response_decode(raw_response.payload, raw_response.payload_length,
                                      &query_response)) {
        tm_ipc_client_response_destroy(&raw_response);
        return TM_CLI_PROTOCOL_ERROR;
    }
    tm_ipc_client_response_destroy(&raw_response);
    exit_code = TM_CLI_SUCCESS;
    if (strcmp(command, "show") == 0) {
        exit_code = tm_show_query(&query_response) ? TM_CLI_SUCCESS : TM_CLI_GENERAL_FAILURE;
    } else if (strcmp(command, "json") == 0) {
        if (!tm_json_write_query(&query_response, &output)) {
            exit_code = TM_CLI_GENERAL_FAILURE;
        } else {
            (void)fwrite(output.data, 1, output.length, stdout);
            tm_text_buffer_destroy(&output);
        }
    } else if (!tm_csv_write_query(&query_response, &output)) {
        exit_code = TM_CLI_GENERAL_FAILURE;
    } else {
        (void)fwrite(output.data, 1, output.length, stdout);
        tm_text_buffer_destroy(&output);
    }
    tm_ipc_query_response_destroy(&query_response);
    return exit_code;
}

static void tm_put_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

int tm_cli_main(int argc, char **argv, bool allow_test_commands)
{
    const char *socket_path = "/run/trafficmon/trafficmon.sock";
    const char *command;
    int command_index = 1;
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("trafficctl " TRAFFICMON_VERSION);
        return TM_CLI_SUCCESS;
    }
    if (argc > 3 && strcmp(argv[1], "--socket") == 0) {
        socket_path = argv[2];
        command_index = 3;
    }
    if (command_index >= argc) {
        tm_cli_usage();
        return TM_CLI_INVALID_USAGE;
    }
    command = argv[command_index];
    if (strcmp(command, "status") == 0 && command_index + 1 == argc) {
        return tm_simple_command(socket_path, TM_IPC_COMMAND_STATUS, NULL, 0, true);
    }
    if (strcmp(command, "list") == 0 && command_index + 1 == argc) {
        return tm_simple_command(socket_path, TM_IPC_COMMAND_LIST_PERIODS, NULL, 0, true);
    }
    if (strcmp(command, "commit") == 0 && command_index + 1 == argc) {
        int result = tm_simple_command(socket_path, TM_IPC_COMMAND_COMMIT, NULL, 0, false);
        if (result == TM_CLI_SUCCESS) {
            puts("committed");
        }
        return result;
    }
    if (strcmp(command, "show") == 0 || strcmp(command, "json") == 0 ||
        strcmp(command, "csv") == 0) {
        return tm_query_command(socket_path, command, argc, argv, command_index + 1);
    }
    if (allow_test_commands && strcmp(command, "__test_seed") == 0 && command_index + 1 == argc) {
        return tm_simple_command(socket_path, TM_IPC_COMMAND_TEST_SEED, NULL, 0, false);
    }
    if (allow_test_commands && strcmp(command, "__test_stop") == 0 && command_index + 1 == argc) {
        return tm_simple_command(socket_path, TM_IPC_COMMAND_TEST_STOP, NULL, 0, false);
    }
    if (allow_test_commands && strcmp(command, "__test_date") == 0 &&
        command_index + 2 == argc) {
        tm_date date;
        uint8_t payload[4];
        if (!tm_date_parse(argv[command_index + 1], &date)) {
            return TM_CLI_INVALID_USAGE;
        }
        tm_put_u32(payload, tm_date_encode(date));
        return tm_simple_command(socket_path, TM_IPC_COMMAND_TEST_DATE, payload, sizeof(payload),
                                 false);
    }
    tm_cli_usage();
    return TM_CLI_INVALID_USAGE;
}
