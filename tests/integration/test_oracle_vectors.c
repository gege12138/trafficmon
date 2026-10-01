#include "aggregator.h"
#include "classifier.h"
#include "generated_vectors.h"
#include "protocol_db.h"
#include "subnet.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

static bool parse_mac(const char *text, uint8_t mac[TM_MAC_LENGTH])
{
    unsigned int values[TM_MAC_LENGTH];
    size_t index;
    if (text == NULL) {
        memset(mac, 0, TM_MAC_LENGTH);
        return false;
    }
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != TM_MAC_LENGTH) {
        return false;
    }
    for (index = 0; index < TM_MAC_LENGTH; index++) {
        mac[index] = (uint8_t)values[index];
    }
    return true;
}

static uint16_t protocol_id_by_name(const tm_protocol_db *database, const char *name)
{
    size_t index;
    for (index = 0; index < database->protocol_count; index++) {
        if (strcmp(database->names[index], name) == 0) {
            return (uint16_t)index;
        }
    }
    return TM_PROTOCOL_MAX;
}

static tm_raw_flow raw_flow_from_oracle(const tm_oracle_flow *source)
{
    tm_raw_flow flow;
    memset(&flow, 0, sizeof(flow));
    flow.family = source->family;
    flow.l4_proto = source->l4_proto;
    flow.orig_src_port = source->orig_src_port;
    flow.orig_dst_port = source->orig_dst_port;
    flow.original = (tm_counter){source->orig_bytes, source->orig_packets};
    flow.reply = (tm_counter){source->reply_bytes, source->reply_packets};
    flow.mac_known = parse_mac(source->mac, flow.host_mac);
    flow.new_connection = source->new_connection;
    TM_EXPECT_TRUE(tm_ip_parse(flow.family, source->orig_src, &flow.orig_src));
    TM_EXPECT_TRUE(tm_ip_parse(flow.family, source->orig_dst, &flow.orig_dst));
    return flow;
}

static void check_expected_entry(const tm_aggregator *aggregator, const tm_protocol_db *protocols,
                                 const tm_oracle_expected *expected)
{
    tm_aggregate_key key;
    const tm_aggregate_entry *entry;
    memset(&key, 0, sizeof(key));
    key.family = expected->family;
    key.protocol_id = protocol_id_by_name(protocols, expected->protocol);
    key.mac_known = parse_mac(expected->mac, key.mac);
    TM_EXPECT_TRUE(key.protocol_id != TM_PROTOCOL_MAX);
    TM_EXPECT_TRUE(tm_ip_parse(key.family, expected->ip, &key.host_ip));
    entry = tm_aggregator_find(aggregator, &key);
    TM_EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return;
    }
    TM_EXPECT_EQ_U64(expected->connections, entry->connections);
    TM_EXPECT_EQ_U64(expected->rx_bytes, entry->rx_bytes);
    TM_EXPECT_EQ_U64(expected->rx_packets, entry->rx_packets);
    TM_EXPECT_EQ_U64(expected->tx_bytes, entry->tx_bytes);
    TM_EXPECT_EQ_U64(expected->tx_packets, entry->tx_packets);
}

void test_oracle_vectors(void)
{
    tm_subnet_set networks;
    tm_protocol_db protocols;
    size_t index;

    tm_subnet_set_init(&networks);
    for (index = 0; index < sizeof(TM_ORACLE_NETWORKS) / sizeof(TM_ORACLE_NETWORKS[0]); index++) {
        TM_EXPECT_TRUE(tm_subnet_set_add(&networks, TM_ORACLE_NETWORKS[index]));
    }
    tm_protocol_db_init(&protocols);
    for (index = 0; index < sizeof(TM_ORACLE_PROTOCOLS) / sizeof(TM_ORACLE_PROTOCOLS[0]); index++) {
        TM_EXPECT_TRUE(tm_protocol_db_add(&protocols, TM_ORACLE_PROTOCOLS[index].name,
                                          TM_ORACLE_PROTOCOLS[index].l4_proto,
                                          TM_ORACLE_PROTOCOLS[index].port));
    }

    for (index = 0; index < sizeof(TM_ORACLE_SCENARIOS) / sizeof(TM_ORACLE_SCENARIOS[0]); index++) {
        const tm_oracle_scenario *scenario = &TM_ORACLE_SCENARIOS[index];
        tm_aggregator aggregator;
        size_t flow_index;
        size_t expected_index;
        TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 100));
        for (flow_index = 0; flow_index < scenario->flow_count; flow_index++) {
            tm_raw_flow flow = raw_flow_from_oracle(&scenario->flows[flow_index]);
            tm_classified_delta classified;
            tm_classify_result result =
                tm_classify_flow(&flow, &networks, &protocols, &classified);
            if (scenario->expected_count == 0) {
                TM_EXPECT_EQ_U64(TM_CLASSIFY_IGNORED, result);
            } else {
                TM_EXPECT_EQ_U64(TM_CLASSIFY_ACCEPTED, result);
                if (result == TM_CLASSIFY_ACCEPTED) {
                    TM_EXPECT_TRUE(
                        tm_aggregator_add(&aggregator, &classified.delta, classified.protocol_id));
                }
            }
        }
        TM_EXPECT_EQ_U64(scenario->expected_count, aggregator.count);
        for (expected_index = 0; expected_index < scenario->expected_count; expected_index++) {
            check_expected_entry(&aggregator, &protocols, &scenario->expected[expected_index]);
        }
        tm_aggregator_destroy(&aggregator);
    }
}
