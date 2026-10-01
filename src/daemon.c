#include "daemon.h"

#include "database.h"
#include "device_query.h"
#include "collector.h"
#include "ipc_protocol.h"
#include "ipc_server.h"
#include "period_store.h"
#include "protocol_db.h"
#include "query.h"
#include "subnet.h"
#include "text_buffer.h"
#include "version.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TM_DAEMON_VERSION_MAJOR = 0, TM_DAEMON_VERSION_MINOR = 3 };

typedef struct {
    const char *database_directory;
    const char *socket_path;
    const char *period_rule;
    const char *protocol_database;
    tm_date current_date;
    size_t record_limit;
    size_t generations;
    size_t netlink_receive_buffer;
    unsigned int refresh_seconds;
    unsigned int commit_seconds;
    char local_interfaces[TM_LOCAL_INTERFACE_LIMIT][32];
    size_t local_interface_count;
    const char *static_hostnames;
    const char *dhcp_leases;
    tm_subnet_set local_networks;
    bool date_set;
    bool debug;
    bool allow_test_hooks;
} tm_daemon_options;

typedef struct {
    tm_period_store store;
    tm_protocol_db protocols;
    tm_collector collector;
    tm_identity identity;
    volatile sig_atomic_t stop_requested;
    time_t started_at;
    time_t next_commit;
    time_t next_period_check;
    unsigned int commit_seconds;
    bool debug;
    bool allow_test_hooks;
} tm_daemon_state;

static volatile sig_atomic_t *tm_signal_stop_flag;

static void tm_signal_handler(int signal_number)
{
    (void)signal_number;
    if (tm_signal_stop_flag != NULL) {
        *tm_signal_stop_flag = 1;
    }
}

static bool tm_parse_size(const char *text, size_t maximum, size_t *value)
{
    char *end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = (size_t)parsed;
    return true;
}

static bool tm_today(tm_date *date)
{
    time_t now = time(NULL);
    struct tm local;
#ifdef _WIN32
    if (localtime_s(&local, &now) != 0) {
        return false;
    }
#else
    if (localtime_r(&now, &local) == NULL) {
        return false;
    }
#endif
    *date = (tm_date){local.tm_year + 1900, (uint8_t)(local.tm_mon + 1), (uint8_t)local.tm_mday};
    return true;
}

static bool tm_parse_options(int argc, char **argv, bool allow_test_hooks,
                             tm_daemon_options *options)
{
    int index;
    memset(options, 0, sizeof(*options));
    options->database_directory = "/var/lib/trafficmon";
    options->socket_path = "/run/trafficmon/trafficmon.sock";
    options->period_rule = "1";
    options->record_limit = 10000;
    options->generations = 10;
    options->netlink_receive_buffer = 524288;
    options->refresh_seconds = 30;
    options->commit_seconds = 86400;
    tm_subnet_set_init(&options->local_networks);
    options->allow_test_hooks = allow_test_hooks;
    for (index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--debug") == 0) {
            options->debug = true;
            continue;
        }
        if (index + 1 >= argc) {
            return false;
        }
        if (strcmp(argument, "--database-dir") == 0 && argv[index + 1][0] != '\0') {
            options->database_directory = argv[++index];
        } else if (strcmp(argument, "--socket") == 0 && argv[index + 1][0] != '\0') {
            options->socket_path = argv[++index];
        } else if (strcmp(argument, "--period-rule") == 0) {
            options->period_rule = argv[++index];
        } else if (strcmp(argument, "--protocol-db") == 0) {
            options->protocol_database = argv[++index];
        } else if (strcmp(argument, "--date") == 0) {
            if (!allow_test_hooks || !tm_date_parse(argv[++index], &options->current_date)) {
                return false;
            }
            options->date_set = true;
        } else if (strcmp(argument, "--limit") == 0) {
            if (!tm_parse_size(argv[++index], TM_DATABASE_HARD_RECORD_LIMIT,
                               &options->record_limit) || options->record_limit == 0) {
                return false;
            }
        } else if (strcmp(argument, "--generations") == 0) {
            if (!tm_parse_size(argv[++index], TM_PERIOD_LIST_MAX, &options->generations) ||
                options->generations == 0) {
                return false;
            }
        } else if (strcmp(argument, "--local-subnet") == 0) {
            if (!tm_subnet_set_add(&options->local_networks, argv[++index])) {
                return false;
            }
        } else if (strcmp(argument, "--local-interface") == 0) {
            const char *name = argv[++index];
            if (options->local_interface_count == TM_LOCAL_INTERFACE_LIMIT ||
                name[0] == '\0' || strlen(name) >= 32) {
                return false;
            }
            strcpy(options->local_interfaces[options->local_interface_count++], name);
        } else if (strcmp(argument, "--static-hostnames") == 0) {
            options->static_hostnames = argv[++index];
        } else if (strcmp(argument, "--dhcp-leases") == 0) {
            options->dhcp_leases = argv[++index];
        } else if (strcmp(argument, "--refresh-seconds") == 0) {
            size_t parsed;
            if (!tm_parse_size(argv[++index], 86400, &parsed) || parsed == 0) {
                return false;
            }
            options->refresh_seconds = (unsigned int)parsed;
        } else if (strcmp(argument, "--netlink-buffer") == 0) {
            if (!tm_parse_size(argv[++index], 64U * 1024U * 1024U,
                               &options->netlink_receive_buffer) ||
                options->netlink_receive_buffer == 0) {
                return false;
            }
        } else if (strcmp(argument, "--commit-seconds") == 0) {
            size_t parsed;
            if (!tm_parse_size(argv[++index], 7U * 86400U, &parsed) || parsed == 0) {
                return false;
            }
            options->commit_seconds = (unsigned int)parsed;
        } else {
            return false;
        }
    }
    return options->date_set || tm_today(&options->current_date);
}

static bool tm_response_text(tm_ipc_server_response *response, tm_text_buffer *text)
{
    response->payload = (uint8_t *)text->data;
    response->payload_length = text->length;
    response->status = TM_IPC_STATUS_OK;
    memset(text, 0, sizeof(*text));
    return true;
}

static bool tm_dispatch_status(tm_daemon_state *state, const tm_ipc_header *request,
                               tm_ipc_server_response *response)
{
    tm_text_buffer text;
    char start[11];
    char end[11];
    time_t now = time(NULL);
    uint64_t uptime = now >= state->started_at ? (uint64_t)(now - state->started_at) : 0;
    memset(&text, 0, sizeof(text));
    if (request->payload_length != 0 || !tm_date_format(state->store.metadata.period.start, start) ||
        !tm_date_format(state->store.metadata.period.end, end) || !tm_text_buffer_init(&text) ||
        !tm_text_buffer_printf(
            &text,
            "{\"daemon_version\":\"%d.%d\",\"database_format_version\":%d,"
            "\"current_period\":{\"start\":\"%s\",\"end\":\"%s\"},"
            "\"record_count\":%zu,\"capacity\":%zu,\"dropped_new_keys\":%llu,"
            "\"uptime_seconds\":%llu,\"collector_enabled\":%s,"
            "\"collector_state\":\"%s\",\"netlink_receive_buffer\":%zu,"
            "\"netlink_messages\":%llu,\"netlink_parse_errors\":%llu,"
            "\"netlink_enobufs\":%llu,\"conntrack_new_events\":%llu,"
            "\"conntrack_destroy_events\":%llu,\"conntrack_dump_count\":%llu,"
            "\"conntrack_dump_errors\":%llu,\"last_successful_dump\":%lld,"
            "\"neighbor_cache_entries\":%zu,\"unresolved_addresses\":%zu,"
            "\"hostname_cache_entries\":%zu}\n",
            TM_DAEMON_VERSION_MAJOR, TM_DAEMON_VERSION_MINOR, TM_DATABASE_FORMAT_VERSION, start, end,
            state->store.aggregator.count, state->store.record_limit,
            (unsigned long long)state->store.aggregator.dropped_new_keys,
            (unsigned long long)uptime, state->collector.stats.enabled ? "true" : "false",
            tm_collector_state(&state->collector), state->collector.stats.receive_buffer,
            (unsigned long long)state->collector.stats.netlink_messages,
            (unsigned long long)state->collector.stats.netlink_parse_errors,
            (unsigned long long)state->collector.stats.netlink_enobufs,
            (unsigned long long)state->collector.stats.conntrack_new_events,
            (unsigned long long)state->collector.stats.conntrack_destroy_events,
            (unsigned long long)state->collector.stats.conntrack_dump_count,
            (unsigned long long)state->collector.stats.conntrack_dump_errors,
            (long long)state->collector.stats.last_successful_dump,
            state->identity.neighbor_count, state->identity.unresolved_count,
            state->identity.hostname_count)) {
        tm_text_buffer_destroy(&text);
        response->status = request->payload_length == 0 ? TM_IPC_STATUS_INTERNAL_ERROR
                                                        : TM_IPC_STATUS_BAD_REQUEST;
        return true;
    }
    return tm_response_text(response, &text);
}

static bool tm_dispatch_list(tm_daemon_state *state, const tm_ipc_header *request,
                             tm_ipc_server_response *response)
{
    tm_period_descriptor *periods = NULL;
    size_t count;
    tm_text_buffer text;
    memset(&text, 0, sizeof(text));
    if (request->payload_length != 0) {
        response->status = TM_IPC_STATUS_BAD_REQUEST;
        return true;
    }
    if (!tm_period_store_list(&state->store, &periods, &count) || !tm_text_buffer_init(&text) ||
        !tm_text_buffer_append(&text, "{\"periods\":[")) {
        free(periods);
        tm_text_buffer_destroy(&text);
        response->status = TM_IPC_STATUS_DATABASE_ERROR;
        return true;
    }
    for (size_t index = 0; index < count; index++) {
        char start[11];
        char end[11];
        if (!tm_date_format(periods[index].period.start, start) ||
            !tm_date_format(periods[index].period.end, end) ||
            (index != 0 && !tm_text_buffer_append(&text, ",")) ||
            !tm_text_buffer_printf(&text,
                                   "{\"start\":\"%s\",\"end\":\"%s\",\"current\":%s,"
                                   "\"record_count\":%u}",
                                   start, end, periods[index].current ? "true" : "false",
                                   periods[index].record_count)) {
            free(periods);
            tm_text_buffer_destroy(&text);
            response->status = TM_IPC_STATUS_INTERNAL_ERROR;
            return true;
        }
    }
    free(periods);
    if (!tm_text_buffer_append(&text, "]}\n")) {
        tm_text_buffer_destroy(&text);
        response->status = TM_IPC_STATUS_INTERNAL_ERROR;
        return true;
    }
    return tm_response_text(response, &text);
}

static bool tm_dispatch_query(tm_daemon_state *state, const uint8_t *payload, size_t length,
                              tm_ipc_server_response *response)
{
    tm_ipc_query_request request;
    tm_aggregator historical;
    tm_database_metadata metadata;
    const tm_aggregator *source = &state->store.aggregator;
    bool owns_source = false;
    tm_query_result query;
    const char *names[TM_PROTOCOL_MAX];
    if (!tm_ipc_query_request_decode(payload, length, &request)) {
        response->status = TM_IPC_STATUS_BAD_REQUEST;
        return true;
    }
    metadata = state->store.metadata;
    if (request.period_start != 0 &&
        request.period_start != tm_date_encode(state->store.metadata.period.start)) {
        tm_date start;
        tm_database_result load_result;
        if (!tm_date_decode(request.period_start, &start)) {
            response->status = TM_IPC_STATUS_BAD_REQUEST;
            return true;
        }
        load_result = tm_period_store_load(&state->store, start, &historical, &metadata);
        if (load_result != TM_DATABASE_OK) {
            response->status = TM_IPC_STATUS_DATABASE_ERROR;
            return true;
        }
        source = &historical;
        owns_source = true;
    }
    if (!tm_query_run(source, &request.query, &query)) {
        if (owns_source) {
            tm_aggregator_destroy(&historical);
        }
        response->status = TM_IPC_STATUS_QUERY_ERROR;
        return true;
    }
    for (size_t index = 0; index < TM_PROTOCOL_MAX; index++) {
        names[index] = index < state->protocols.protocol_count &&
                               state->protocols.names[index][0] != '\0'
                           ? state->protocols.names[index]
                           : NULL;
    }
    if (!tm_ipc_query_response_encode(tm_date_encode(metadata.period.start),
                                      tm_date_encode(metadata.period.end), &query, names,
                                      state->protocols.protocol_count, &response->payload,
                                      &response->payload_length)) {
        response->status = TM_IPC_STATUS_INTERNAL_ERROR;
    } else {
        response->status = TM_IPC_STATUS_OK;
    }
    tm_query_result_destroy(&query);
    if (owns_source) {
        tm_aggregator_destroy(&historical);
    }
    return true;
}

static bool tm_dispatch_device_query(tm_daemon_state *state, const uint8_t *payload,
                                     size_t length, bool json, bool exact,
                                     tm_ipc_server_response *response)
{
    tm_ipc_query_request request;
    tm_aggregator historical;
    tm_database_metadata metadata = state->store.metadata;
    const tm_aggregator *source = &state->store.aggregator;
    bool owns_source = false;
    tm_text_buffer text;
    char period[11];
    char filter[65];
    const char *selected_filter = NULL;
    if (length < TM_IPC_QUERY_REQUEST_SIZE || length > TM_IPC_QUERY_REQUEST_SIZE + sizeof(filter) ||
        !tm_ipc_query_request_decode(payload, TM_IPC_QUERY_REQUEST_SIZE, &request) ||
        (request.query.group_by & TM_GROUP_DEVICE) == 0) {
        response->status = TM_IPC_STATUS_BAD_REQUEST;
        return true;
    }
    if (length > TM_IPC_QUERY_REQUEST_SIZE) {
        size_t filter_length = length - TM_IPC_QUERY_REQUEST_SIZE - 1;
        if (filter_length == 0 || filter_length >= sizeof(filter) || payload[length - 1] != 0) {
            response->status = TM_IPC_STATUS_BAD_REQUEST;
            return true;
        }
        for (size_t index = 0; index < filter_length; index++) {
            if (payload[TM_IPC_QUERY_REQUEST_SIZE + index] < 0x20 ||
                payload[TM_IPC_QUERY_REQUEST_SIZE + index] > 0x7e) {
                response->status = TM_IPC_STATUS_BAD_REQUEST;
                return true;
            }
        }
        memcpy(filter, payload + TM_IPC_QUERY_REQUEST_SIZE, filter_length + 1);
        selected_filter = filter;
        if (exact) {
            tm_ip_addr address;
            bool valid = false;
            if (filter_length == 17) {
                valid = true;
                for (size_t index = 0; index < 17; index++) {
                    if ((index % 3 == 2 && filter[index] != ':') ||
                        (index % 3 != 2 &&
                         !(filter[index] >= '0' && filter[index] <= '9') &&
                         !(filter[index] >= 'a' && filter[index] <= 'f'))) {
                        valid = false;
                    }
                }
            }
            if (!valid && strncmp(filter, "ip4:", 4) == 0) {
                valid = tm_ip_parse(TM_FAMILY_IPV4, filter + 4, &address);
            }
            if (!valid && strncmp(filter, "ip6:", 4) == 0) {
                valid = tm_ip_parse(TM_FAMILY_IPV6, filter + 4, &address);
            }
            if (!valid) {
                response->status = TM_IPC_STATUS_BAD_REQUEST;
                return true;
            }
        }
    } else if (exact) {
        response->status = TM_IPC_STATUS_BAD_REQUEST;
        return true;
    }
    if (request.period_start != 0 &&
        request.period_start != tm_date_encode(metadata.period.start)) {
        tm_date start;
        if (!tm_date_decode(request.period_start, &start)) {
            response->status = TM_IPC_STATUS_BAD_REQUEST;
            return true;
        }
        if (tm_period_store_load(&state->store, start, &historical, &metadata) != TM_DATABASE_OK) {
            response->status = TM_IPC_STATUS_DATABASE_ERROR;
            return true;
        }
        source = &historical;
        owns_source = true;
    }
    tm_identity_refresh_hostnames(&state->identity, time(NULL));
    if (!tm_date_format(metadata.period.start, period) ||
        !tm_device_query_render(source, &request.query, &state->identity,
                                !owns_source, &state->protocols, period, json,
                                selected_filter, exact, &text)) {
        response->status = TM_IPC_STATUS_QUERY_ERROR;
    } else if (text.length > TM_IPC_MAX_RESPONSE_SIZE) {
        tm_text_buffer_destroy(&text);
        response->status = TM_IPC_STATUS_TOO_LARGE;
    } else {
        (void)tm_response_text(response, &text);
    }
    if (owns_source) {
        tm_aggregator_destroy(&historical);
    }
    return true;
}

static bool tm_test_seed(tm_daemon_state *state)
{
    tm_flow_delta first;
    tm_flow_delta second;
    memset(&first, 0, sizeof(first));
    first.family = TM_FAMILY_IPV4;
    first.mac_known = true;
    first.mac[0] = 0x02;
    first.mac[5] = 0x64;
    first.new_connection = true;
    first.rx_bytes = 123456;
    first.rx_packets = 789;
    first.tx_bytes = 45678;
    first.tx_packets = 321;
    if (!tm_ip_parse(first.family, "192.168.1.100", &first.host_ip)) {
        return false;
    }
    second = first;
    second.mac_known = false;
    memset(second.mac, 0, sizeof(second.mac));
    second.rx_bytes = UINT64_MAX;
    second.rx_packets = UINT32_MAX;
    second.tx_bytes = 1;
    second.tx_packets = 1;
    if (!tm_ip_parse(second.family, "192.168.1.101", &second.host_ip)) {
        return false;
    }
    if (!tm_aggregator_add(&state->store.aggregator, &first,
                           tm_protocol_db_lookup(&state->protocols, 6, 443)) ||
        !tm_aggregator_add(&state->store.aggregator, &second, TM_PROTOCOL_OTHER)) {
        return false;
    }
    return tm_identity_observe_unresolved(&state->identity, second.family,
                                          &second.host_ip, time(NULL));
}

static uint32_t tm_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool tm_daemon_dispatch(const tm_ipc_header *request, const uint8_t *payload,
                               tm_ipc_server_response *response, void *context)
{
    tm_daemon_state *state = context;
    switch (request->command) {
    case TM_IPC_COMMAND_STATUS:
        return tm_dispatch_status(state, request, response);
    case TM_IPC_COMMAND_LIST_PERIODS:
        return tm_dispatch_list(state, request, response);
    case TM_IPC_COMMAND_QUERY:
        return tm_dispatch_query(state, payload, request->payload_length, response);
    case TM_IPC_COMMAND_DEVICE_JSON:
        return tm_dispatch_device_query(state, payload, request->payload_length, true, false, response);
    case TM_IPC_COMMAND_DEVICE_SHOW:
        return tm_dispatch_device_query(state, payload, request->payload_length, false, false, response);
    case TM_IPC_COMMAND_DEVICE_DETAIL_JSON:
        return tm_dispatch_device_query(state, payload, request->payload_length, true, true, response);
    case TM_IPC_COMMAND_COMMIT:
        response->status = request->payload_length == 0 && tm_period_store_commit(&state->store)
                               ? TM_IPC_STATUS_OK
                               : TM_IPC_STATUS_DATABASE_ERROR;
        return true;
    case TM_IPC_COMMAND_PING:
        if (request->payload_length != 0) {
            response->status = TM_IPC_STATUS_BAD_REQUEST;
        } else {
            response->payload = malloc(5);
            if (response->payload == NULL) {
                response->status = TM_IPC_STATUS_INTERNAL_ERROR;
            } else {
                memcpy(response->payload, "pong\n", 5);
                response->payload_length = 5;
                response->status = TM_IPC_STATUS_OK;
            }
        }
        return true;
    case TM_IPC_COMMAND_TEST_SEED:
        response->status = state->allow_test_hooks && request->payload_length == 0 &&
                                   tm_test_seed(state)
                               ? TM_IPC_STATUS_OK
                               : TM_IPC_STATUS_UNKNOWN_COMMAND;
        return true;
    case TM_IPC_COMMAND_TEST_DATE: {
        tm_date date;
        bool rolled_over;
        response->status =
            state->allow_test_hooks && request->payload_length == 4 &&
                    tm_date_decode(tm_read_u32(payload), &date) &&
                    tm_period_store_rollover(&state->store, date, &rolled_over)
                ? TM_IPC_STATUS_OK
                : TM_IPC_STATUS_UNKNOWN_COMMAND;
        return true;
    }
    case TM_IPC_COMMAND_TEST_STOP:
        if (state->allow_test_hooks && request->payload_length == 0) {
            response->status = tm_period_store_commit(&state->store) ? TM_IPC_STATUS_OK
                                                                     : TM_IPC_STATUS_DATABASE_ERROR;
            state->stop_requested = 1;
        } else {
            response->status = TM_IPC_STATUS_UNKNOWN_COMMAND;
        }
        return true;
    default:
        response->status = TM_IPC_STATUS_UNKNOWN_COMMAND;
        return true;
    }
}

static bool tm_daemon_tick(void *context)
{
    tm_daemon_state *state = context;
    time_t now;
    if (!tm_collector_tick(&state->collector)) {
        fprintf(stderr, "trafficmon: collector servicing failed\n");
        return false;
    }
    now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    tm_identity_refresh_hostnames(&state->identity, now);
    if (now >= state->next_period_check) {
        tm_date today;
        bool rolled_over = false;
        state->next_period_check = now + 60;
        if (!tm_today(&today) || !tm_period_store_rollover(&state->store, today, &rolled_over)) {
            fprintf(stderr, "trafficmon: period rollover check failed\n");
            return false;
        }
        if (rolled_over) {
            fprintf(stderr, "trafficmon: accounting period rolled over\n");
        }
    }
    if (now >= state->next_commit) {
        if (!tm_period_store_commit(&state->store)) {
            fprintf(stderr, "trafficmon: periodic database commit failed\n");
            return false;
        }
        state->next_commit = now + (time_t)state->commit_seconds;
        if (state->debug) {
            fprintf(stderr, "trafficmon: periodic database commit completed\n");
        }
    }
    return true;
}

int tm_daemon_main(int argc, char **argv, bool allow_test_hooks)
{
    tm_daemon_options options;
    tm_period_rule rule;
    tm_daemon_state state;
    bool server_result;
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("trafficmon " TRAFFICMON_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--validate-subnet") == 0) {
        tm_subnet subnet;
        if (!tm_subnet_parse(argv[2], &subnet)) {
            fprintf(stderr, "trafficmon: invalid local subnet: %s\n", argv[2]);
            return 2;
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--validate-period") == 0) {
        tm_period_rule validation_rule;
        if (!tm_period_rule_parse(argv[2], &validation_rule)) {
            fprintf(stderr, "trafficmon: invalid accounting period: %s\n", argv[2]);
            return 2;
        }
        return 0;
    }
    if (!tm_parse_options(argc, argv, allow_test_hooks, &options) ||
        !tm_period_rule_parse(options.period_rule, &rule)) {
        fprintf(stderr,
                "usage: trafficmon [--database-dir DIR] [--socket PATH] [--period-rule RULE] "
                "[--protocol-db FILE] [--limit N] [--generations N] "
                "[--local-subnet CIDR] [--local-interface NAME] "
                "[--static-hostnames FILE] [--dhcp-leases FILE] "
                "[--refresh-seconds N] [--netlink-buffer BYTES] "
                "[--commit-seconds N] [--debug]\n");
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.collector.socket_fd = -1;
    state.allow_test_hooks = options.allow_test_hooks;
    state.started_at = time(NULL);
    state.commit_seconds = options.commit_seconds;
    state.next_commit = state.started_at + (time_t)options.commit_seconds;
    state.next_period_check = state.started_at + 60;
    state.debug = options.debug;
    tm_protocol_db_init(&state.protocols);
    if (options.protocol_database != NULL &&
        !tm_protocol_db_load(&state.protocols, options.protocol_database)) {
        fprintf(stderr, "trafficmon: unable to load protocol database\n");
        return 1;
    }
    if (allow_test_hooks &&
        !tm_protocol_db_add_with_id(&state.protocols, 20, "HTTPS", 6, 443)) {
        return 1;
    }
    if (!tm_period_store_init(&state.store, options.database_directory, &rule,
                              options.current_date, options.record_limit, options.generations,
                              tm_protocol_db_fingerprint(&state.protocols))) {
        fprintf(stderr, "trafficmon: unable to initialize database\n");
        return 1;
    }
    if (!tm_identity_init(&state.identity, &options.local_networks,
                          options.static_hostnames, options.dhcp_leases)) {
        tm_period_store_destroy(&state.store);
        return 1;
    }
    tm_identity_bind_aggregator(&state.identity, &state.store.aggregator, options.debug);
    for (size_t index = 0; index < options.local_interface_count; index++) {
        if (!tm_identity_add_interface(&state.identity, options.local_interfaces[index])) {
            fprintf(stderr, "trafficmon: invalid local interface: %s\n",
                    options.local_interfaces[index]);
            tm_identity_destroy(&state.identity);
            tm_period_store_destroy(&state.store);
            return 1;
        }
    }
    (void)tm_identity_refresh_neighbors(&state.identity, time(NULL));
    tm_identity_refresh_hostnames(&state.identity, time(NULL));
    if (options.local_networks.count != 0 &&
        !tm_collector_init(&state.collector, &state.store.aggregator,
                           &options.local_networks, &state.protocols, &state.identity,
                           options.netlink_receive_buffer, options.refresh_seconds)) {
        fprintf(stderr, "trafficmon: unable to initialize conntrack collector\n");
        tm_identity_destroy(&state.identity);
        tm_period_store_destroy(&state.store);
        return 1;
    }
    fprintf(stderr, "trafficmon: started version %s%s\n", TRAFFICMON_VERSION,
            state.collector.stats.enabled ? " with conntrack collector" : "");
    if (state.debug) {
        fprintf(stderr,
                "trafficmon: database=%s socket=%s records=%zu generations=%zu "
                "commit=%u seconds local-subnets=%zu\n",
                options.database_directory, options.socket_path, options.record_limit,
                options.generations, options.commit_seconds, options.local_networks.count);
    }
    tm_signal_stop_flag = &state.stop_requested;
#ifdef _WIN32
    (void)signal(SIGINT, tm_signal_handler);
    (void)signal(SIGTERM, tm_signal_handler);
#else
    {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = tm_signal_handler;
        (void)sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        (void)sigaction(SIGINT, &action, NULL);
        (void)sigaction(SIGTERM, &action, NULL);
    }
#endif
    server_result = tm_ipc_server_run_with_tick(options.socket_path, tm_daemon_dispatch,
                                                tm_daemon_tick, &state,
                                                &state.stop_requested);
    if (!tm_period_store_commit(&state.store)) {
        fprintf(stderr, "trafficmon: shutdown database commit failed\n");
        server_result = false;
    }
    tm_signal_stop_flag = NULL;
    tm_collector_destroy(&state.collector);
    tm_identity_destroy(&state.identity);
    tm_period_store_destroy(&state.store);
    fprintf(stderr, "trafficmon: stopped%s\n", server_result ? " cleanly" : " with errors");
    return server_result ? 0 : 1;
}
