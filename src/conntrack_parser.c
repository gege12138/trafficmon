#include "conntrack_parser.h"

#ifdef __linux__

#include <arpa/inet.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netlink.h>
#include <string.h>

static uint64_t tm_be64(const uint8_t *value)
{
    uint64_t result = 0;
    for (size_t index = 0; index < 8; index++) {
        result = (result << 8) | value[index];
    }
    return result;
}

static bool tm_attr_header(const uint8_t *data, size_t remaining, uint16_t *length,
                           uint16_t *type)
{
    struct nlattr attribute;
    if (remaining < sizeof(attribute)) {
        return false;
    }
    memcpy(&attribute, data, sizeof(attribute));
    if (attribute.nla_len < sizeof(attribute) || attribute.nla_len > remaining) {
        return false;
    }
    *length = attribute.nla_len;
    *type = attribute.nla_type & NLA_TYPE_MASK;
    return true;
}

static bool tm_parse_ip(const uint8_t *data, size_t length, uint8_t family,
                        tm_ip_addr *source, tm_ip_addr *destination)
{
    size_t offset = 0;
    bool have_source = false;
    bool have_destination = false;
    const size_t address_size = family == TM_FAMILY_IPV4 ? 4 : 16;
    while (offset < length) {
        uint16_t attribute_length;
        size_t payload_length;
        uint16_t type;
        if (!tm_attr_header(data + offset, length - offset, &attribute_length, &type)) {
            return false;
        }
        payload_length = attribute_length - sizeof(struct nlattr);
        if ((family == TM_FAMILY_IPV4 && type == CTA_IP_V4_SRC) ||
            (family == TM_FAMILY_IPV6 && type == CTA_IP_V6_SRC)) {
            if (payload_length != address_size || have_source) {
                return false;
            }
            memcpy(source->bytes, data + offset + sizeof(struct nlattr), address_size);
            have_source = true;
        } else if ((family == TM_FAMILY_IPV4 && type == CTA_IP_V4_DST) ||
                   (family == TM_FAMILY_IPV6 && type == CTA_IP_V6_DST)) {
            if (payload_length != address_size || have_destination) {
                return false;
            }
            memcpy(destination->bytes, data + offset + sizeof(struct nlattr), address_size);
            have_destination = true;
        }
        offset += NLA_ALIGN(attribute_length);
        if (offset > length) {
            return false;
        }
    }
    return offset == length && have_source && have_destination;
}

static bool tm_parse_proto(const uint8_t *data, size_t length, uint8_t *protocol,
                           uint16_t *source_port, uint16_t *destination_port)
{
    size_t offset = 0;
    bool have_protocol = false;
    while (offset < length) {
        uint16_t attribute_length;
        size_t payload_length;
        uint16_t type;
        const uint8_t *payload;
        if (!tm_attr_header(data + offset, length - offset, &attribute_length, &type)) {
            return false;
        }
        payload_length = attribute_length - sizeof(struct nlattr);
        payload = data + offset + sizeof(struct nlattr);
        if (type == CTA_PROTO_NUM) {
            if (payload_length != 1 || have_protocol) {
                return false;
            }
            *protocol = payload[0];
            have_protocol = true;
        } else if (type == CTA_PROTO_SRC_PORT || type == CTA_PROTO_DST_PORT) {
            uint16_t network_port;
            if (payload_length != sizeof(network_port)) {
                return false;
            }
            memcpy(&network_port, payload, sizeof(network_port));
            if (type == CTA_PROTO_SRC_PORT) {
                *source_port = ntohs(network_port);
            } else {
                *destination_port = ntohs(network_port);
            }
        }
        offset += NLA_ALIGN(attribute_length);
        if (offset > length) {
            return false;
        }
    }
    return offset == length && have_protocol;
}

static bool tm_parse_tuple(const uint8_t *data, size_t length, uint8_t family,
                           tm_ip_addr *source, tm_ip_addr *destination, uint8_t *protocol,
                           uint16_t *source_port, uint16_t *destination_port)
{
    size_t offset = 0;
    bool have_ip = false;
    bool have_proto = false;
    while (offset < length) {
        uint16_t attribute_length;
        size_t payload_length;
        uint16_t type;
        const uint8_t *payload;
        if (!tm_attr_header(data + offset, length - offset, &attribute_length, &type)) {
            return false;
        }
        payload_length = attribute_length - sizeof(struct nlattr);
        payload = data + offset + sizeof(struct nlattr);
        if (type == CTA_TUPLE_IP) {
            if (have_ip || !tm_parse_ip(payload, payload_length, family, source, destination)) {
                return false;
            }
            have_ip = true;
        } else if (type == CTA_TUPLE_PROTO) {
            if (have_proto || !tm_parse_proto(payload, payload_length, protocol, source_port,
                                              destination_port)) {
                return false;
            }
            have_proto = true;
        }
        offset += NLA_ALIGN(attribute_length);
        if (offset > length) {
            return false;
        }
    }
    return offset == length && have_ip && have_proto;
}

static bool tm_parse_counters(const uint8_t *data, size_t length, tm_counter *counter)
{
    size_t offset = 0;
    while (offset < length) {
        uint16_t attribute_length;
        size_t payload_length;
        uint16_t type;
        const uint8_t *payload;
        if (!tm_attr_header(data + offset, length - offset, &attribute_length, &type)) {
            return false;
        }
        payload_length = attribute_length - sizeof(struct nlattr);
        payload = data + offset + sizeof(struct nlattr);
        if (type == CTA_COUNTERS_PACKETS || type == CTA_COUNTERS_BYTES) {
            if (payload_length != 8) {
                return false;
            }
            if (type == CTA_COUNTERS_PACKETS) {
                counter->packets = tm_be64(payload);
            } else {
                counter->bytes = tm_be64(payload);
            }
        }
        offset += NLA_ALIGN(attribute_length);
        if (offset > length) {
            return false;
        }
    }
    return offset == length;
}

bool tm_conntrack_parse_message(const uint8_t *data, size_t length,
                                tm_conntrack_record *record)
{
    struct nlmsghdr header;
    struct nfgenmsg generic;
    size_t offset;
    bool have_original = false;
    bool have_reply = false;
    uint8_t reply_protocol = 0;
    if (data == NULL || record == NULL || length < NLMSG_LENGTH(sizeof(generic))) {
        return false;
    }
    memcpy(&header, data, sizeof(header));
    if (header.nlmsg_len < NLMSG_LENGTH(sizeof(generic)) || header.nlmsg_len > length) {
        return false;
    }
    memcpy(&generic, data + NLMSG_HDRLEN, sizeof(generic));
    memset(record, 0, sizeof(*record));
    if (generic.nfgen_family == AF_INET) {
        record->flow.family = TM_FAMILY_IPV4;
    } else if (generic.nfgen_family == AF_INET6) {
        record->flow.family = TM_FAMILY_IPV6;
    } else {
        return false;
    }
    offset = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(generic)));
    while (offset < header.nlmsg_len) {
        uint16_t attribute_length;
        size_t payload_length;
        uint16_t type;
        const uint8_t *payload;
        if (!tm_attr_header(data + offset, header.nlmsg_len - offset, &attribute_length, &type)) {
            return false;
        }
        payload_length = attribute_length - sizeof(struct nlattr);
        payload = data + offset + sizeof(struct nlattr);
        if (type == CTA_TUPLE_ORIG) {
            if (have_original ||
                !tm_parse_tuple(payload, payload_length, record->flow.family,
                                &record->flow.orig_src, &record->flow.orig_dst,
                                &record->flow.l4_proto, &record->flow.orig_src_port,
                                &record->flow.orig_dst_port)) {
                return false;
            }
            have_original = true;
        } else if (type == CTA_TUPLE_REPLY) {
            if (have_reply ||
                !tm_parse_tuple(payload, payload_length, record->flow.family,
                                &record->reply_src, &record->reply_dst, &reply_protocol,
                                &record->reply_src_port, &record->reply_dst_port)) {
                return false;
            }
            have_reply = true;
        } else if (type == CTA_COUNTERS_ORIG) {
            if (!tm_parse_counters(payload, payload_length, &record->flow.original)) {
                return false;
            }
        } else if (type == CTA_COUNTERS_REPLY) {
            if (!tm_parse_counters(payload, payload_length, &record->flow.reply)) {
                return false;
            }
        }
        offset += NLA_ALIGN(attribute_length);
        if (offset > header.nlmsg_len) {
            return false;
        }
    }
    return offset == header.nlmsg_len && have_original && have_reply &&
           reply_protocol == record->flow.l4_proto;
}

#else

bool tm_conntrack_parse_message(const uint8_t *data, size_t length,
                                tm_conntrack_record *record)
{
    (void)data;
    (void)length;
    (void)record;
    return false;
}

#endif
