/* Disposable-QEMU acceptance fixture; never install this utility on a router. */
#include "aggregator.h"
#include "database.h"
#include "protocol_db.h"
#include "storage.h"
#include "subnet.h"

#include <stdio.h>
#include <string.h>

static bool add(tm_aggregator *aggregator, unsigned int device, unsigned int address,
                bool known, uint8_t family, const char *ip, uint16_t protocol)
{
    tm_flow_delta delta = {0};
    delta.family = family;
    delta.mac_known = known;
    delta.mac[0] = 0x02;
    delta.mac[3] = (uint8_t)(device >> 8);
    delta.mac[4] = (uint8_t)device;
    delta.mac[5] = 0x01;
    delta.new_connection = true;
    delta.rx_bytes = 1000 + device + address;
    delta.tx_bytes = 200 + address;
    delta.rx_packets = 2;
    delta.tx_packets = 1;
    return tm_ip_parse(family, ip, &delta.host_ip) &&
           tm_aggregator_add(aggregator, &delta, protocol);
}

static bool write_database(const char *path, const tm_aggregator *aggregator,
                           tm_period period, uint64_t fingerprint)
{
    tm_database_metadata metadata = {.period = period,
                                     .protocol_fingerprint = fingerprint};
    return tm_storage_commit_database(path, aggregator, &metadata);
}

int main(int argc, char **argv)
{
    tm_protocol_db protocols;
    tm_aggregator current, history;
    char current_path[TM_PATH_MAX], history_path[TM_PATH_MAX];
    const uint16_t ids[] = {10, 20, 30, 40, 0};
    if (argc != 3 || !tm_protocol_db_load(&protocols, argv[1]) ||
        snprintf(current_path, sizeof(current_path), "%s/current.db", argv[2]) >=
            (int)sizeof(current_path) ||
        snprintf(history_path, sizeof(history_path), "%s/period-20260801.db", argv[2]) >=
            (int)sizeof(history_path) ||
        !tm_aggregator_init(&current, 10000) || !tm_aggregator_init(&history, 10000)) {
        return 1;
    }
    for (unsigned int device = 0; device < 499; device++) {
        char ip[64];
        (void)snprintf(ip, sizeof(ip), "10.123.%u.%u", device / 250U,
                       device % 250U + 1U);
        if (!add(&current, device, 0, true, TM_FAMILY_IPV4, ip, 20)) {
            return 2;
        }
        for (unsigned int address = 1; address <= 19; address++) {
            (void)snprintf(ip, sizeof(ip), "fd12:3456:%x::%x", device + 1U, address);
            if (!add(&current, device, address, true, TM_FAMILY_IPV6, ip,
                     ids[address % 5U])) {
                return 3;
            }
        }
    }
    if (!add(&current, 499, 0, false, TM_FAMILY_IPV4, "10.124.0.219", 20)) {
        return 4;
    }
    for (unsigned int device = 0; device < 19; device++) {
        char ip[64];
        (void)snprintf(ip, sizeof(ip), "fd12:3456:ffff:%x::1", device + 1U);
        if (!add(&current, device, 20, true, TM_FAMILY_IPV6, ip, 20)) {
            return 5;
        }
    }
    if (current.count != 10000 ||
        !add(&history, 0, 0, true, TM_FAMILY_IPV4, "10.123.0.1", 20) ||
        !add(&history, 0, 1, true, TM_FAMILY_IPV6, "fd12:3456:1::1", 30) ||
        !add(&history, 499, 0, false, TM_FAMILY_IPV4, "10.124.0.219", 0) ||
        !write_database(current_path, &current,
                        (tm_period){{2026, 9, 1}, {2026, 10, 1}},
                        tm_protocol_db_fingerprint(&protocols)) ||
        !write_database(history_path, &history,
                        (tm_period){{2026, 8, 1}, {2026, 9, 1}},
                        tm_protocol_db_fingerprint(&protocols))) {
        return 6;
    }
    printf("current_records=%zu historical_records=%zu\n", current.count, history.count);
    tm_aggregator_destroy(&history);
    tm_aggregator_destroy(&current);
    return 0;
}
