#include "classifier.h"

#include <string.h>

tm_classify_result tm_classify_flow(const tm_raw_flow *flow, const tm_subnet_set *local_networks,
                                    const tm_protocol_db *protocols,
                                    tm_classified_delta *classified)
{
    bool source_local;
    bool destination_local;

    if (flow == NULL || local_networks == NULL || classified == NULL ||
        !tm_family_is_valid(flow->family)) {
        return TM_CLASSIFY_INVALID;
    }
    source_local = tm_subnet_set_contains(local_networks, flow->family, &flow->orig_src);
    destination_local = tm_subnet_set_contains(local_networks, flow->family, &flow->orig_dst);
    if (source_local == destination_local) {
        return TM_CLASSIFY_IGNORED;
    }

    memset(classified, 0, sizeof(*classified));
    classified->delta.family = flow->family;
    classified->delta.l4_proto = flow->l4_proto;
    classified->delta.service_port = flow->orig_dst_port;
    classified->delta.mac_known = flow->mac_known;
    if (flow->mac_known) {
        memcpy(classified->delta.mac, flow->host_mac, sizeof(classified->delta.mac));
    }
    classified->delta.new_connection = flow->new_connection;

    if (source_local) {
        classified->delta.host_ip = flow->orig_src;
        classified->delta.tx_bytes = flow->original.bytes;
        classified->delta.tx_packets = flow->original.packets;
        classified->delta.rx_bytes = flow->reply.bytes;
        classified->delta.rx_packets = flow->reply.packets;
    } else {
        classified->delta.host_ip = flow->orig_dst;
        classified->delta.rx_bytes = flow->original.bytes;
        classified->delta.rx_packets = flow->original.packets;
        classified->delta.tx_bytes = flow->reply.bytes;
        classified->delta.tx_packets = flow->reply.packets;
    }
    classified->protocol_id =
        tm_protocol_db_lookup(protocols, flow->l4_proto, classified->delta.service_port);
    return TM_CLASSIFY_ACCEPTED;
}
