#include "classifier.h"
#include "conntrack_parser.h"
#include "protocol_db.h"
#include "subnet.h"
#include "test.h"

#include <arpa/inet.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netlink.h>
#include <string.h>

typedef struct {
    _Alignas(struct nlmsghdr) uint8_t data[1024];
    size_t length;
} fixture;

static size_t nested_begin(fixture *item, uint16_t type)
{
    size_t start = item->length;
    struct nlattr attribute = {(uint16_t)sizeof(attribute), (uint16_t)(type | NLA_F_NESTED)};
    memcpy(item->data + start, &attribute, sizeof(attribute));
    item->length += NLA_ALIGN(sizeof(attribute));
    return start;
}

static void nested_end(fixture *item, size_t start)
{
    struct nlattr attribute;
    memcpy(&attribute, item->data + start, sizeof(attribute));
    attribute.nla_len = (uint16_t)(item->length - start);
    memcpy(item->data + start, &attribute, sizeof(attribute));
}

static void add_attr(fixture *item, uint16_t type, const void *data, size_t length)
{
    struct nlattr attribute;
    size_t total = sizeof(attribute) + length;
    attribute.nla_len = (uint16_t)total;
    attribute.nla_type = type;
    memcpy(item->data + item->length, &attribute, sizeof(attribute));
    memcpy(item->data + item->length + sizeof(attribute), data, length);
    item->length += NLA_ALIGN(total);
}

static void add_be64(fixture *item, uint16_t type, uint64_t value)
{
    uint8_t bytes[8];
    for (size_t index = 0; index < 8; index++) {
        bytes[7 - index] = (uint8_t)(value >> (index * 8));
    }
    add_attr(item, type, bytes, sizeof(bytes));
}

static void add_tuple(fixture *item, uint16_t type, int family, const char *source,
                      const char *destination, uint8_t protocol, uint16_t source_port,
                      uint16_t destination_port)
{
    uint8_t source_bytes[16] = {0};
    uint8_t destination_bytes[16] = {0};
    uint16_t network_port;
    size_t tuple = nested_begin(item, type);
    size_t ip = nested_begin(item, CTA_TUPLE_IP);
    TM_EXPECT_TRUE(inet_pton(family, source, source_bytes) == 1);
    TM_EXPECT_TRUE(inet_pton(family, destination, destination_bytes) == 1);
    add_attr(item, family == AF_INET ? CTA_IP_V4_SRC : CTA_IP_V6_SRC, source_bytes,
             family == AF_INET ? 4 : 16);
    add_attr(item, family == AF_INET ? CTA_IP_V4_DST : CTA_IP_V6_DST, destination_bytes,
             family == AF_INET ? 4 : 16);
    nested_end(item, ip);
    size_t proto = nested_begin(item, CTA_TUPLE_PROTO);
    add_attr(item, CTA_PROTO_NUM, &protocol, 1);
    network_port = htons(source_port);
    add_attr(item, CTA_PROTO_SRC_PORT, &network_port, sizeof(network_port));
    network_port = htons(destination_port);
    add_attr(item, CTA_PROTO_DST_PORT, &network_port, sizeof(network_port));
    nested_end(item, proto);
    nested_end(item, tuple);
}

static fixture make_fixture(int family, const char *source, const char *destination,
                            uint8_t protocol, uint16_t destination_port, bool counters)
{
    fixture item;
    struct nlmsghdr header;
    struct nfgenmsg generic;
    const char *reply_source = destination;
    const char *reply_destination = source;
    memset(&item, 0, sizeof(item));
    memset(&header, 0, sizeof(header));
    memset(&generic, 0, sizeof(generic));
    item.length = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(generic)));
    header.nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW;
    generic.nfgen_family = (uint8_t)family;
    generic.version = NFNETLINK_V0;
    memcpy(item.data, &header, sizeof(header));
    memcpy(item.data + NLMSG_HDRLEN, &generic, sizeof(generic));
    add_tuple(&item, CTA_TUPLE_ORIG, family, source, destination, protocol, 40000,
              destination_port);
    add_tuple(&item, CTA_TUPLE_REPLY, family, reply_source, reply_destination, protocol,
              destination_port, 40000);
    if (counters) {
        size_t original = nested_begin(&item, CTA_COUNTERS_ORIG);
        add_be64(&item, CTA_COUNTERS_PACKETS, 7);
        add_be64(&item, CTA_COUNTERS_BYTES, UINT64_C(0x100000002));
        nested_end(&item, original);
        size_t reply = nested_begin(&item, CTA_COUNTERS_REPLY);
        add_be64(&item, CTA_COUNTERS_PACKETS, 9);
        add_be64(&item, CTA_COUNTERS_BYTES, UINT64_C(0x200000003));
        nested_end(&item, reply);
    }
    {
        uint32_t unknown = 123;
        add_attr(&item, 0x3fff, &unknown, sizeof(unknown));
    }
    header.nlmsg_len = (uint32_t)item.length;
    memcpy(item.data, &header, sizeof(header));
    return item;
}

static void expect_valid(int family, const char *source, const char *destination,
                         uint8_t protocol, uint16_t port)
{
    fixture item = make_fixture(family, source, destination, protocol, port, true);
    tm_conntrack_record record;
    TM_EXPECT_TRUE(tm_conntrack_parse_message(item.data, item.length, &record));
    TM_EXPECT_EQ_U64(family == AF_INET ? TM_FAMILY_IPV4 : TM_FAMILY_IPV6,
                     record.flow.family);
    TM_EXPECT_EQ_U64(protocol, record.flow.l4_proto);
    TM_EXPECT_EQ_U64(port, record.flow.orig_dst_port);
    TM_EXPECT_EQ_U64(UINT64_C(0x100000002), record.flow.original.bytes);
    TM_EXPECT_EQ_U64(UINT64_C(0x200000003), record.flow.reply.bytes);
}

void test_conntrack_parser(void)
{
    fixture item;
    tm_conntrack_record record;
    tm_subnet_set subnets;
    tm_protocol_db protocols;
    tm_classified_delta classified;

    expect_valid(AF_INET, "192.168.50.2", "198.51.100.2", 6, 443);
    expect_valid(AF_INET, "198.51.100.2", "192.168.50.2", 6, 22);
    expect_valid(AF_INET, "192.168.50.2", "198.51.100.2", 17, 53);
    expect_valid(AF_INET6, "fd50::2", "2001:db8:51::2", 6, 443);
    expect_valid(AF_INET6, "2001:db8:51::2", "fd50::2", 6, 22);
    expect_valid(AF_INET6, "fd50::2", "2001:db8:51::2", 17, 53);
    expect_valid(AF_INET, "192.168.50.2", "198.51.100.2", 1, 0);
    expect_valid(AF_INET6, "fd50::2", "2001:db8:51::2", 58, 0);

    item = make_fixture(AF_INET, "192.168.50.2", "198.51.100.2", 6, 443, false);
    TM_EXPECT_TRUE(tm_conntrack_parse_message(item.data, item.length, &record));
    TM_EXPECT_EQ_U64(0, record.flow.original.bytes);
    TM_EXPECT_EQ_U64(0, record.flow.reply.packets);

    tm_subnet_set_init(&subnets);
    TM_EXPECT_TRUE(tm_subnet_set_add(&subnets, "192.168.50.0/24"));
    tm_protocol_db_init(&protocols);
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&protocols, 20, "HTTPS", 6, 443));
    item = make_fixture(AF_INET, "192.168.50.2", "198.51.100.2", 6, 443, true);
    TM_EXPECT_TRUE(tm_conntrack_parse_message(item.data, item.length, &record));
    TM_EXPECT_EQ_U64(TM_CLASSIFY_ACCEPTED,
                     tm_classify_flow(&record.flow, &subnets, &protocols, &classified));
    TM_EXPECT_EQ_U64(20, classified.protocol_id);
    TM_EXPECT_EQ_U64(record.flow.original.bytes, classified.delta.tx_bytes);

    TM_EXPECT_TRUE(!tm_conntrack_parse_message(item.data, 8, &record));
    {
        struct nlmsghdr header;
        memcpy(&header, item.data, sizeof(header));
        header.nlmsg_len = (uint32_t)item.length + 1;
        memcpy(item.data, &header, sizeof(header));
    }
    TM_EXPECT_TRUE(!tm_conntrack_parse_message(item.data, item.length, &record));
    {
        struct nlmsghdr header;
        struct nfgenmsg generic;
        memcpy(&header, item.data, sizeof(header));
        header.nlmsg_len = (uint32_t)item.length;
        memcpy(item.data, &header, sizeof(header));
        memcpy(&generic, item.data + NLMSG_HDRLEN, sizeof(generic));
        generic.nfgen_family = AF_UNSPEC;
        memcpy(item.data + NLMSG_HDRLEN, &generic, sizeof(generic));
    }
    TM_EXPECT_TRUE(!tm_conntrack_parse_message(item.data, item.length, &record));
    {
        struct nfgenmsg generic;
        memcpy(&generic, item.data + NLMSG_HDRLEN, sizeof(generic));
        generic.nfgen_family = AF_INET;
        memcpy(item.data + NLMSG_HDRLEN, &generic, sizeof(generic));
    }
    {
        size_t first_offset = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct nfgenmsg)));
        struct nlattr first;
        uint16_t saved;
        memcpy(&first, item.data + first_offset, sizeof(first));
        saved = first.nla_len;
        first.nla_len = 2;
        memcpy(item.data + first_offset, &first, sizeof(first));
        TM_EXPECT_TRUE(!tm_conntrack_parse_message(item.data, item.length, &record));
        first.nla_len = saved;
        first.nla_len = UINT16_MAX;
        memcpy(item.data + first_offset, &first, sizeof(first));
        TM_EXPECT_TRUE(!tm_conntrack_parse_message(item.data, item.length, &record));
    }
}
