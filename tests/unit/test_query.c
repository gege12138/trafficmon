#include "aggregator.h"
#include "query.h"
#include "subnet.h"
#include "test.h"

#include <string.h>

static void add_query_delta(tm_aggregator *aggregator, const char *ip, uint16_t protocol,
                            uint64_t rx, uint64_t tx)
{
    tm_flow_delta delta;
    memset(&delta, 0, sizeof(delta));
    delta.family = TM_FAMILY_IPV4;
    delta.new_connection = true;
    delta.rx_bytes = rx;
    delta.tx_bytes = tx;
    TM_EXPECT_TRUE(tm_ip_parse(delta.family, ip, &delta.host_ip));
    TM_EXPECT_TRUE(tm_aggregator_add(aggregator, &delta, protocol));
}

void test_query(void)
{
    tm_aggregator aggregator;
    tm_query_spec spec;
    tm_query_result result;

    TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 10));
    add_query_delta(&aggregator, "192.168.1.1", 1, 100, 20);
    add_query_delta(&aggregator, "192.168.1.1", 2, 50, 10);
    add_query_delta(&aggregator, "192.168.1.2", 1, 300, 30);

    memset(&spec, 0, sizeof(spec));
    spec.group_by = TM_GROUP_FAMILY | TM_GROUP_IP;
    spec.order = TM_ORDER_TOTAL_BYTES;
    spec.descending = true;
    TM_EXPECT_TRUE(tm_query_run(&aggregator, &spec, &result));
    TM_EXPECT_EQ_U64(2, result.total_count);
    TM_EXPECT_EQ_U64(2, result.count);
    if (result.count == 2) {
        TM_EXPECT_EQ_U64(300, result.rows[0].rx_bytes);
        TM_EXPECT_EQ_U64(150, result.rows[1].rx_bytes);
        TM_EXPECT_EQ_U64(2, result.rows[1].connections);
    }
    tm_query_result_destroy(&result);

    spec.group_by = TM_GROUP_PROTOCOL;
    spec.limit = 0;
    spec.offset = 0;
    TM_EXPECT_TRUE(tm_query_run(&aggregator, &spec, &result));
    TM_EXPECT_EQ_U64(2, result.total_count);
    TM_EXPECT_EQ_U64(2, result.count);
    if (result.count == 2) {
        TM_EXPECT_EQ_U64(400, result.rows[0].rx_bytes);
        TM_EXPECT_EQ_U64(50, result.rows[1].rx_bytes);
    }
    tm_query_result_destroy(&result);

    spec.group_by = TM_GROUP_FAMILY | TM_GROUP_IP;
    spec.limit = 1;
    spec.offset = 1;
    TM_EXPECT_TRUE(tm_query_run(&aggregator, &spec, &result));
    TM_EXPECT_EQ_U64(2, result.total_count);
    TM_EXPECT_EQ_U64(1, result.count);
    if (result.count == 1) {
        TM_EXPECT_EQ_U64(150, result.rows[0].rx_bytes);
    }
    tm_query_result_destroy(&result);
    tm_aggregator_destroy(&aggregator);
}
