#include "flow.h"
#include "test.h"

#include <stdint.h>

void test_flow_types(void)
{
    tm_flow_delta delta = {0};
    delta.family = TM_FAMILY_IPV6;
    delta.rx_bytes = UINT64_MAX;

    TM_EXPECT_TRUE(tm_family_is_valid(TM_FAMILY_IPV4));
    TM_EXPECT_TRUE(tm_family_is_valid(TM_FAMILY_IPV6));
    TM_EXPECT_TRUE(!tm_family_is_valid(0));
    TM_EXPECT_EQ_U64(4, tm_ip_size(TM_FAMILY_IPV4));
    TM_EXPECT_EQ_U64(16, tm_ip_size(TM_FAMILY_IPV6));
    TM_EXPECT_EQ_U64(0, tm_ip_size(0));
    TM_EXPECT_EQ_U64(UINT64_MAX, delta.rx_bytes);
    TM_EXPECT_EQ_U64(TM_MAC_LENGTH, sizeof(delta.mac));
    TM_EXPECT_EQ_U64(TM_IPV6_LENGTH, sizeof(delta.host_ip.bytes));
}
