#include "aggregator.h"
#include "device_query.h"
#include "identity.h"
#include "subnet.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned long vmrss_kb(void)
{
    FILE *input = fopen("/proc/self/status", "r");
    char line[128];
    unsigned long value = 0;
    if (input == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        if (sscanf(line, "VmRSS: %lu kB", &value) == 1) {
            break;
        }
    }
    (void)fclose(input);
    return value;
}

static size_t count_rows(const char *json)
{
    size_t count = 0;
    const char *cursor = json;
    while ((cursor = strstr(cursor, "\"device_id\":")) != NULL) {
        count++;
        cursor++;
    }
    return count;
}

int main(void)
{
    tm_subnet_set networks;
    tm_identity identity;
    tm_aggregator aggregator;
    time_t now = time(NULL);
    tm_subnet_set_init(&networks);
    if (!tm_subnet_set_add(&networks, "10.0.0.0/16") ||
        !tm_subnet_set_add(&networks, "2001:db8::/32") ||
        !tm_identity_init(&identity, &networks, NULL, NULL) ||
        !tm_aggregator_init(&aggregator, 10000)) {
        return 1;
    }
    printf("devices=0 rss_kb=%lu neighbors=%zu hostnames=%zu records=%zu\n",
           vmrss_kb(), identity.neighbor_count, identity.hostname_count, aggregator.count);
    for (unsigned int index = 0; index < 500; index++) {
        uint8_t mac[TM_MAC_LENGTH] = {2, 0, 0, (uint8_t)(index >> 8), (uint8_t)index, 1};
        char ip4[32], ip6[64], name[32];
        tm_flow_delta delta;
        memset(&delta, 0, sizeof(delta));
        delta.family = TM_FAMILY_IPV4;
        delta.mac_known = true;
        memcpy(delta.mac, mac, TM_MAC_LENGTH);
        delta.rx_bytes = 1024;
        (void)snprintf(ip4, sizeof(ip4), "10.0.%u.%u", index / 250U, index % 250U + 1U);
        (void)snprintf(ip6, sizeof(ip6), "2001:db8::%x", index + 1U);
        (void)snprintf(name, sizeof(name), "device-%u", index + 1U);
        if (!tm_ip_parse(TM_FAMILY_IPV4, ip4, &delta.host_ip) ||
            !tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &delta.host_ip, mac, now) ||
            !tm_identity_hostname_put(&identity, mac, 0, NULL, name, 1) ||
            !tm_aggregator_add(&aggregator, &delta, 20)) {
            return 2;
        }
        delta.family = TM_FAMILY_IPV6;
        if (!tm_ip_parse(TM_FAMILY_IPV6, ip6, &delta.host_ip) ||
            !tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &delta.host_ip, mac, now) ||
            !tm_aggregator_add(&aggregator, &delta, 20)) {
            return 3;
        }
        if (index + 1U == 50U || index + 1U == 100U || index + 1U == 500U) {
            tm_query_spec spec = {.group_by = TM_GROUP_DEVICE, .order = TM_ORDER_TOTAL_BYTES,
                                  .descending = true};
            tm_text_buffer output;
            if (!tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                        "2026-09-01", true, NULL, false, &output)) {
                return 4;
            }
            printf("devices=%u rss_kb=%lu neighbors=%zu hostnames=%zu records=%zu json_bytes=%zu\n",
                   index + 1U, vmrss_kb(), identity.neighbor_count,
                   identity.hostname_count, aggregator.count, output.length);
            tm_text_buffer_destroy(&output);
        }
    }
    for (unsigned int device = 0; device < 500; device++) {
        uint8_t mac[TM_MAC_LENGTH] = {2, 0, 0, (uint8_t)(device >> 8),
                                     (uint8_t)device, 1};
        for (unsigned int address = 0; address < 18; address++) {
            tm_flow_delta delta;
            char ip6[64];
            memset(&delta, 0, sizeof(delta));
            delta.family = TM_FAMILY_IPV6;
            delta.mac_known = true;
            memcpy(delta.mac, mac, TM_MAC_LENGTH);
            delta.rx_bytes = 1024;
            (void)snprintf(ip6, sizeof(ip6), "2001:db8:1:%x::%x", device + 1U,
                           address + 1U);
            if (!tm_ip_parse(TM_FAMILY_IPV6, ip6, &delta.host_ip) ||
                !tm_aggregator_add(&aggregator, &delta, 20)) {
                return 5;
            }
        }
    }
    {
        tm_query_spec spec = {.group_by = TM_GROUP_DEVICE, .order = TM_ORDER_TOTAL_BYTES,
                              .descending = true, .limit = 50, .offset = 450};
        tm_text_buffer output;
        clock_t start = clock();
        if (aggregator.count != 10000 ||
            !tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                    "2026-09-01", true, NULL, false, &output)) {
            return 6;
        }
        if (strstr(output.data, "\"total_count\":500") == NULL ||
            count_rows(output.data) != 50) {
            return 8;
        }
        printf("devices=500 rss_kb=%lu records=%zu page_offset=450 page_limit=50 "
               "page_rows=%zu response_bytes=%zu cpu_ms=%.1f\n", vmrss_kb(),
               aggregator.count, count_rows(output.data), output.length,
               1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC);
        tm_text_buffer_destroy(&output);
        start = clock();
        spec.offset = 0;
        if (!tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                    "2026-09-01", true, "device-42", false, &output)) {
            return 7;
        }
        printf("search=device-42 response_bytes=%zu cpu_ms=%.1f\n", output.length,
               1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC);
        tm_text_buffer_destroy(&output);
    }
    tm_aggregator_destroy(&aggregator);
    tm_identity_destroy(&identity);
    return 0;
}
