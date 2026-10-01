#include "aggregator.h"
#include "device_query.h"
#include "identity.h"
#include "query.h"
#include "subnet.h"
#include "test.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#endif

#ifdef __linux__
static void emit_neighbor(tm_identity *identity, const tm_ip_addr *ip,
                          uint8_t family, const uint8_t mac[TM_MAC_LENGTH], uint16_t state,
                          bool include_mac, time_t now)
{
    union { struct nlmsghdr alignment; uint8_t bytes[128]; } buffer;
    struct nlmsghdr *header = (struct nlmsghdr *)buffer.bytes;
    struct ndmsg *neighbor;
    struct rtattr *attribute;
    memset(&buffer, 0, sizeof(buffer));
    header->nlmsg_type = RTM_NEWNEIGH;
    header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
    neighbor = NLMSG_DATA(header);
    size_t ip_length = tm_ip_size(family);
    neighbor->ndm_family = family == TM_FAMILY_IPV4 ? AF_INET : AF_INET6;
    neighbor->ndm_ifindex = (int)if_nametoindex("lo");
    neighbor->ndm_state = state;
    attribute = (struct rtattr *)(buffer.bytes + NLMSG_ALIGN(header->nlmsg_len));
    attribute->rta_type = NDA_DST;
    attribute->rta_len = RTA_LENGTH(ip_length);
    memcpy(RTA_DATA(attribute), ip->bytes, ip_length);
    header->nlmsg_len = NLMSG_ALIGN(header->nlmsg_len) + RTA_LENGTH(ip_length);
    if (include_mac) {
        attribute = (struct rtattr *)(buffer.bytes + NLMSG_ALIGN(header->nlmsg_len));
        attribute->rta_type = NDA_LLADDR;
        attribute->rta_len = RTA_LENGTH(TM_MAC_LENGTH);
        memcpy(RTA_DATA(attribute), mac, TM_MAC_LENGTH);
        header->nlmsg_len = NLMSG_ALIGN(header->nlmsg_len) + RTA_LENGTH(TM_MAC_LENGTH);
    }
    (void)tm_identity_consume_neighbor_message(identity, buffer.bytes, header->nlmsg_len, now);
}
#endif

static void add_delta(tm_aggregator *aggregator, uint8_t family, const char *ip,
                      const uint8_t mac[TM_MAC_LENGTH], bool mac_known, uint64_t bytes)
{
    tm_flow_delta delta;
    memset(&delta, 0, sizeof(delta));
    delta.family = family;
    delta.mac_known = mac_known;
    if (mac_known) {
        memcpy(delta.mac, mac, TM_MAC_LENGTH);
    }
    delta.rx_bytes = bytes;
    TM_EXPECT_TRUE(tm_ip_parse(family, ip, &delta.host_ip));
    TM_EXPECT_TRUE(tm_aggregator_add(aggregator, &delta, 20));
}

static void test_delayed_resolution_and_privacy(void)
{
    tm_subnet_set networks;
    tm_identity identity;
    tm_aggregator aggregator;
    tm_query_spec spec = {.group_by = TM_GROUP_DEVICE, .order = TM_ORDER_TOTAL_BYTES,
                          .descending = true};
    tm_text_buffer rendered;
    tm_ip_addr ip_a, ip_b, ip_c, ip_new, unknown[3], changed_mac_ip;
    uint8_t mac1[TM_MAC_LENGTH] = {2, 0, 0, 0, 1, 1};
    uint8_t mac2[TM_MAC_LENGTH] = {2, 0, 0, 0, 2, 2};
    time_t now = time(NULL);

    tm_subnet_set_init(&networks);
    TM_EXPECT_TRUE(tm_subnet_set_add(&networks, "2001:db8:1::/64"));
    TM_EXPECT_TRUE(tm_subnet_set_add(&networks, "2001:db8:2::/64"));
    TM_EXPECT_TRUE(tm_identity_init(&identity, &networks, NULL, NULL));
    TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 128));
    tm_identity_bind_aggregator(&identity, &aggregator, false);

    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1::100", &ip_a));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1::200", &ip_b));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1::300", &ip_c));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:2::987", &ip_new));
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1::100", mac1, false, 100);
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1::100", mac1, true, 25);
    TM_EXPECT_TRUE(tm_identity_observe_unresolved(&identity, TM_FAMILY_IPV6, &ip_a, now));
    TM_EXPECT_EQ_U64(1, identity.unresolved_count);

    /* Test 2: a later NDP mapping migrates legacy/unresolved counters once. */
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip_a, mac1, now + 1));
    TM_EXPECT_EQ_U64(0, identity.unresolved_count);
    TM_EXPECT_EQ_U64(1, aggregator.count);
    {
        tm_aggregate_key key;
        const tm_aggregate_entry *entry;
        memset(&key, 0, sizeof(key));
        key.family = TM_FAMILY_IPV6;
        key.host_ip = ip_a;
        key.mac_known = true;
        memcpy(key.mac, mac1, TM_MAC_LENGTH);
        key.protocol_id = 20;
        entry = tm_aggregator_find(&aggregator, &key);
        TM_EXPECT_TRUE(entry != NULL);
        if (entry != NULL) {
            TM_EXPECT_EQ_U64(125, entry->rx_bytes);
        }
    }

    /* Test 1: several privacy addresses mapped by NDP remain one MAC device. */
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1::200", mac1, true, 20);
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1::300", mac1, true, 30);
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip_b, mac1, now + 2));
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip_c, mac1, now + 2));

    /* Test 3: a new PD prefix maps naturally through the same MAC. */
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:2::987", mac1, true, 40);
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip_new, mac1, now + 3));
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, NULL, false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    tm_text_buffer_destroy(&rendered);

    /* Tests 4/5: unresolved privacy addresses are tracked, but are not devices. */
    for (size_t index = 0; index < 3; index++) {
        char text[48];
        (void)snprintf(text, sizeof(text), "2001:db8:1::%x", (unsigned int)(0x400 + index));
        TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, text, &unknown[index]));
        add_delta(&aggregator, TM_FAMILY_IPV6, text, mac1, false, 10);
        TM_EXPECT_TRUE(tm_identity_observe_unresolved(&identity, TM_FAMILY_IPV6,
                                                       &unknown[index], now + 4));
    }
    TM_EXPECT_EQ_U64(3, identity.unresolved_count);
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, NULL, false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    TM_EXPECT_TRUE(strstr(rendered.data, "ip6:") == NULL);
    tm_text_buffer_destroy(&rendered);
    tm_identity_expire(&identity, now + 4 + TM_UNRESOLVED_TTL_SECONDS);
    TM_EXPECT_EQ_U64(0, identity.unresolved_count);

#ifdef __linux__
    /* Test 6: usable NUD transitions retain the same identity. */
    TM_EXPECT_TRUE(tm_identity_add_interface(&identity, "lo"));
    emit_neighbor(&identity, &ip_b, TM_FAMILY_IPV6, mac1, NUD_REACHABLE, true, now + 5);
    emit_neighbor(&identity, &ip_b, TM_FAMILY_IPV6, mac1, NUD_STALE, true, now + 6);
    emit_neighbor(&identity, &ip_b, TM_FAMILY_IPV6, mac1, NUD_DELAY, true, now + 7);
#endif

    /* Test 7: a real MAC change is never merged by hostname or IPv6 suffix. */
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:2::200", &changed_mac_ip));
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:2::200", mac2, true, 50);
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6,
                                             &changed_mac_ip, mac2, now + 8));
    TM_EXPECT_TRUE(tm_identity_hostname_put(&identity, mac1, 0, NULL, "phone", 1));
    TM_EXPECT_TRUE(tm_identity_hostname_put(&identity, mac2, 0, NULL, "phone", 1));
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, NULL, false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":2") != NULL);
    tm_text_buffer_destroy(&rendered);

    tm_aggregator_destroy(&aggregator);
    tm_identity_destroy(&identity);
}

void test_identity(void)
{
    tm_subnet_set networks;
    tm_identity identity;
    tm_ip_addr ip4, ip6, ip6_privacy, ip_other;
    uint8_t mac1[TM_MAC_LENGTH] = {2, 0, 0, 0, 0, 1};
    uint8_t mac2[TM_MAC_LENGTH] = {2, 0, 0, 0, 0, 2};
    uint8_t actual[TM_MAC_LENGTH];
    tm_aggregator aggregator;
    tm_query_spec spec = {.group_by = TM_GROUP_DEVICE, .order = TM_ORDER_TOTAL_BYTES,
                          .descending = true};
    tm_query_result result;
    tm_text_buffer rendered;
    char static_path[512], lease_path[512];
    time_t now = time(NULL);

    tm_subnet_set_init(&networks);
    TM_EXPECT_TRUE(tm_subnet_set_add(&networks, "10.0.0.0/8"));
    TM_EXPECT_TRUE(tm_subnet_set_add(&networks, "2001:db8:1234:5678::abcd/64"));
    (void)snprintf(static_path, sizeof(static_path), "%s/fixtures/dhcp.config",
                   TM_TEST_SOURCE_DIR);
    (void)snprintf(lease_path, sizeof(lease_path), "%s/fixtures/dhcp.leases",
                   TM_TEST_SOURCE_DIR);
    TM_EXPECT_TRUE(tm_identity_init(&identity, &networks, static_path, lease_path));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV4, "10.0.0.20", &ip4));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1234:5678::1234", &ip6));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV6, "2001:db8:1234:5678::5678", &ip6_privacy));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV4, "172.16.0.1", &ip_other));
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &ip4, mac1, now));
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip6, mac1, now));
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV6, &ip6_privacy, mac1, now));
    TM_EXPECT_TRUE(!tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &ip_other, mac1, now));
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now, actual));
    TM_EXPECT_MEMORY_EQ(mac1, actual, TM_MAC_LENGTH);

#ifdef __linux__
    TM_EXPECT_TRUE(tm_identity_add_interface(&identity, "lo"));
    emit_neighbor(&identity, &ip4, TM_FAMILY_IPV4, mac2, NUD_REACHABLE, true, now);
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now, actual));
    TM_EXPECT_MEMORY_EQ(mac2, actual, TM_MAC_LENGTH);
    emit_neighbor(&identity, &ip4, TM_FAMILY_IPV4, mac2, NUD_FAILED, false, now);
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now, actual));
    emit_neighbor(&identity, &ip4, TM_FAMILY_IPV4, mac2, NUD_INCOMPLETE, false, now);
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now, actual));
    emit_neighbor(&identity, &ip4, TM_FAMILY_IPV4, mac1, NUD_PERMANENT, true, now);
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now, actual));
    TM_EXPECT_MEMORY_EQ(mac1, actual, TM_MAC_LENGTH);
#endif
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV6, &ip6, now, actual));
    TM_EXPECT_MEMORY_EQ(mac1, actual, TM_MAC_LENGTH);
    TM_EXPECT_TRUE(!tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip_other, now, actual));

    tm_identity_refresh_hostnames(&identity, now);
    TM_EXPECT_TRUE(strcmp(tm_identity_hostname(&identity, true, mac1, 0, NULL), "Static-NAS") == 0);
    TM_EXPECT_TRUE(strcmp(tm_identity_hostname(&identity, true, mac2, 0, NULL), "Pixel-10") == 0);
    TM_EXPECT_TRUE(tm_identity_hostname(&identity, false, NULL, TM_FAMILY_IPV4, &ip_other) == NULL);
    {
        uint8_t expired_mac[TM_MAC_LENGTH] = {2, 0, 0, 0, 0, 4};
        TM_EXPECT_TRUE(tm_identity_hostname(&identity, true, expired_mac, 0, NULL) == NULL);
    }
    TM_EXPECT_TRUE(tm_identity_hostname_put(&identity, mac2, 0, NULL, "New-Name", 1));
    TM_EXPECT_TRUE(strcmp(tm_identity_hostname(&identity, true, mac2, 0, NULL), "New-Name") == 0);
    TM_EXPECT_TRUE(tm_identity_hostname_put(&identity, mac2, 0, NULL, "Lower-Priority", 0));
    TM_EXPECT_TRUE(strcmp(tm_identity_hostname(&identity, true, mac2, 0, NULL), "New-Name") == 0);

    TM_EXPECT_TRUE(tm_aggregator_init(&aggregator, 128));
    add_delta(&aggregator, TM_FAMILY_IPV4, "10.0.0.20", mac1, false, 100);
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1234:5678::1234", mac1, true, 200);
    add_delta(&aggregator, TM_FAMILY_IPV6, "2001:db8:1234:5678::5678", mac1, false, 300);
    add_delta(&aggregator, TM_FAMILY_IPV4, "10.0.0.31", mac1, false, 25);
    TM_EXPECT_TRUE(tm_query_run_with_identity(&aggregator, &spec, &identity, &result));
    TM_EXPECT_EQ_U64(2, result.total_count);
    TM_EXPECT_EQ_U64(600, result.rows[0].rx_bytes);
    TM_EXPECT_TRUE(result.rows[0].key.mac_known);
    TM_EXPECT_EQ_U64(25, result.rows[1].rx_bytes);
    TM_EXPECT_TRUE(!result.rows[1].key.mac_known);
    tm_query_result_destroy(&result);
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, NULL, false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "Static-NAS") != NULL);
    TM_EXPECT_TRUE(strstr(rendered.data, "2001:db8:1234:5678::5678") != NULL);
    TM_EXPECT_TRUE(strstr(rendered.data, "ip4:10.0.0.31") == NULL);
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    tm_text_buffer_destroy(&rendered);

    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, "static-nas", false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    TM_EXPECT_TRUE(strstr(rendered.data, "ip4:10.0.0.31") == NULL);
    tm_text_buffer_destroy(&rendered);
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, "2001:db8:1234:5678::5678",
                                          false, &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    tm_text_buffer_destroy(&rendered);
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, "02:00:00:00:00:01", true,
                                          &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":1") != NULL);
    tm_text_buffer_destroy(&rendered);
    TM_EXPECT_TRUE(tm_device_query_render(&aggregator, &spec, &identity, true, NULL,
                                          "2026-09-01", true, "02:00:00:00:00:02", true,
                                          &rendered));
    TM_EXPECT_TRUE(strstr(rendered.data, "\"total_count\":0") != NULL);
    tm_text_buffer_destroy(&rendered);

    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &ip4, mac2, now + 2));
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now + 3, actual));
    TM_EXPECT_MEMORY_EQ(mac2, actual, TM_MAC_LENGTH);
    TM_EXPECT_TRUE(!tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4,
                                               now + 2 + TM_NEIGHBOR_TTL_SECONDS, actual));
    TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &ip4, mac1, now + 500));
    TM_EXPECT_TRUE(tm_identity_neighbor_get(&identity, TM_FAMILY_IPV4, &ip4, now + 501, actual));
    TM_EXPECT_MEMORY_EQ(mac1, actual, TM_MAC_LENGTH);

    for (unsigned int index = 0; index < TM_NEIGHBOR_CACHE_LIMIT + 100U; index++) {
        tm_ip_addr many = {{10, 1, (uint8_t)(index >> 8), (uint8_t)index}};
        TM_EXPECT_TRUE(tm_identity_neighbor_put(&identity, TM_FAMILY_IPV4, &many, mac1, now + 600));
    }
    TM_EXPECT_EQ_U64(TM_NEIGHBOR_CACHE_LIMIT, identity.neighbor_count);
    tm_aggregator_destroy(&aggregator);
    tm_identity_destroy(&identity);
    test_delayed_resolution_and_privacy();
}
