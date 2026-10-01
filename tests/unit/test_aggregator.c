#include "aggregator.h"
#include "subnet.h"
#include "test.h"

#include <stdint.h>
#include <string.h>

static tm_flow_delta make_delta(const char *ip, uint16_t marker, uint64_t bytes)
{
    tm_flow_delta delta;
    memset(&delta, 0, sizeof(delta));
    delta.family = TM_FAMILY_IPV4;
    delta.mac_known = true;
    delta.mac[5] = (uint8_t)marker;
    delta.new_connection = true;
    delta.rx_bytes = bytes;
    TM_EXPECT_TRUE(tm_ip_parse(delta.family, ip, &delta.host_ip));
    return delta;
}

void test_aggregator(void)
{
    tm_aggregator aggregator;
    tm_flow_delta first = make_delta("192.168.1.1", 1, UINT64_MAX);
    tm_flow_delta second = make_delta("192.168.1.2", 2, 20);
    tm_flow_delta third = make_delta("192.168.1.3", 3, 30);
    tm_aggregate_key key;
    const tm_aggregate_entry *entry;

    TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 2));
    TM_EXPECT_TRUE(tm_aggregator_add(&aggregator, &first, 1));
    TM_EXPECT_TRUE(tm_aggregator_add(&aggregator, &second, 1));
    TM_EXPECT_TRUE(!tm_aggregator_add(&aggregator, &third, 1));
    TM_EXPECT_EQ_U64(2, aggregator.count);
    TM_EXPECT_EQ_U64(1, aggregator.dropped_new_keys);

    first.new_connection = false;
    first.rx_bytes = 1;
    TM_EXPECT_TRUE(tm_aggregator_add(&aggregator, &first, 1));
    memset(&key, 0, sizeof(key));
    key.family = first.family;
    key.host_ip = first.host_ip;
    memcpy(key.mac, first.mac, sizeof(key.mac));
    key.mac_known = true;
    key.protocol_id = 1;
    entry = tm_aggregator_find(&aggregator, &key);
    TM_EXPECT_TRUE(entry != NULL);
    if (entry != NULL) {
        TM_EXPECT_EQ_U64(UINT64_MAX, entry->rx_bytes);
        TM_EXPECT_EQ_U64(1, entry->connections);
    }
    tm_aggregator_destroy(&aggregator);

    TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 0));
    for (uint8_t index = 1; index <= 40; index++) {
        tm_flow_delta delta = make_delta("192.168.2.1", index, index);
        delta.host_ip.bytes[3] = index;
        TM_EXPECT_TRUE(tm_aggregator_add(&aggregator, &delta, 1));
    }
    TM_EXPECT_EQ_U64(40, aggregator.count);
    TM_EXPECT_TRUE(aggregator.capacity > 16);
    tm_aggregator_destroy(&aggregator);
}
