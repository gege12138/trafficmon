#include "subnet.h"
#include "test.h"

void test_subnets(void)
{
    tm_subnet ipv4;
    tm_subnet ipv6;
    tm_ip_addr address;

    TM_EXPECT_TRUE(tm_subnet_parse("192.168.0.0/16", &ipv4));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV4, "192.168.255.254", &address));
    TM_EXPECT_TRUE(tm_subnet_contains(&ipv4, TM_FAMILY_IPV4, &address));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV4, "192.169.0.1", &address));
    TM_EXPECT_TRUE(!tm_subnet_contains(&ipv4, TM_FAMILY_IPV4, &address));

    TM_EXPECT_TRUE(tm_subnet_parse("2001:db8:abcd::/48", &ipv6));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:abcd:ffff::1", &address));
    TM_EXPECT_TRUE(tm_subnet_contains(&ipv6, TM_FAMILY_IPV6, &address));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:abce::1", &address));
    TM_EXPECT_TRUE(!tm_subnet_contains(&ipv6, TM_FAMILY_IPV6, &address));

    TM_EXPECT_TRUE(!tm_subnet_parse("192.168.1.1/33", &ipv4));
    TM_EXPECT_TRUE(!tm_subnet_parse("2001:db8::/129", &ipv6));
    TM_EXPECT_TRUE(!tm_subnet_parse("not-a-network", &ipv4));

    TM_EXPECT_TRUE(tm_subnet_parse("2001:db8:1234:5678::abcd/64", &ipv6));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1234:5678::", &address));
    TM_EXPECT_MEMORY_EQ(address.bytes, ipv6.network.bytes, TM_IPV6_LENGTH);
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1234:5678::beef", &address));
    TM_EXPECT_TRUE(tm_subnet_contains(&ipv6, TM_FAMILY_IPV6, &address));
}
