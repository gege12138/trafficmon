#include "collector.h"

#include "classifier.h"
#include "conntrack_parser.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

enum { TM_COLLECTOR_BUFFER_DEFAULT = 524288, TM_COLLECTOR_PACKET_MAX = 65536 };

static bool tm_collector_request_dump(tm_collector *collector)
{
    struct {
        struct nlmsghdr header;
        struct nfgenmsg message;
    } request;
    struct sockaddr_nl kernel;
    ssize_t sent;
    memset(&request, 0, sizeof(request));
    memset(&kernel, 0, sizeof(kernel));
    kernel.nl_family = AF_NETLINK;
    collector->sequence++;
    if (collector->sequence == 0) {
        collector->sequence++;
    }
    collector->dump_sequence = collector->sequence;
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.message));
    request.header.nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET_CTRZERO;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = collector->dump_sequence;
    request.message.nfgen_family = AF_UNSPEC;
    request.message.version = NFNETLINK_V0;
    request.message.res_id = htons(0);
    sent = sendto(collector->socket_fd, &request, request.header.nlmsg_len, 0,
                  (const struct sockaddr *)&kernel, sizeof(kernel));
    if (sent != (ssize_t)request.header.nlmsg_len) {
        collector->stats.conntrack_dump_errors++;
        collector->stats.degraded = true;
        fprintf(stderr, "trafficmon: conntrack CTRZERO dump request failed: %s\n",
                strerror(errno));
        return false;
    }
    collector->dump_active = true;
    collector->next_dump = time(NULL) + collector->refresh_seconds;
    return true;
}

static void tm_collector_account(tm_collector *collector, const struct nlmsghdr *header,
                                 bool new_connection, bool journal_event)
{
    tm_conntrack_record record;
    tm_classified_delta classified;
    time_t now;
    if (!tm_conntrack_parse_message((const uint8_t *)header, header->nlmsg_len, &record)) {
        collector->stats.netlink_parse_errors++;
        return;
    }
    record.flow.new_connection = new_connection;
    if (!new_connection && !journal_event && record.flow.original.bytes == 0 &&
        record.flow.original.packets == 0 && record.flow.reply.bytes == 0 &&
        record.flow.reply.packets == 0) {
        return;
    }
    switch (tm_classify_flow(&record.flow, collector->local_networks, collector->protocols,
                             &classified)) {
    case TM_CLASSIFY_ACCEPTED:
        now = time(NULL);
        if (collector->identity != NULL &&
            tm_identity_neighbor_get(collector->identity, classified.delta.family,
                                     &classified.delta.host_ip, now,
                                     classified.delta.mac)) {
            classified.delta.mac_known = true;
        } else if (collector->identity != NULL) {
            (void)tm_identity_observe_unresolved(collector->identity,
                                                 classified.delta.family,
                                                 &classified.delta.host_ip, now);
        }
        (void)tm_aggregator_add(collector->aggregator, &classified.delta,
                               classified.protocol_id);
        if (journal_event && collector->journal != NULL) {
            tm_journal_event event;
            if (tm_journal_event_from_flow(&record.flow, &classified.delta, now, &event)) {
                (void)tm_journal_enqueue(collector->journal, &event);
            }
        }
        break;
    case TM_CLASSIFY_INVALID:
        collector->stats.netlink_parse_errors++;
        break;
    case TM_CLASSIFY_IGNORED:
    default:
        break;
    }
}

static void tm_collector_message(tm_collector *collector, const struct nlmsghdr *header)
{
    uint8_t command;
    bool dump_message = header->nlmsg_seq == collector->dump_sequence && collector->dump_active;
    collector->stats.netlink_messages++;
    if (header->nlmsg_type == NLMSG_DONE) {
        if (dump_message) {
            collector->dump_active = false;
            collector->stats.conntrack_dump_count++;
            collector->stats.last_successful_dump = time(NULL);
            if (collector->stats.netlink_enobufs == 0) {
                collector->stats.degraded = false;
            }
        }
        return;
    }
    if (header->nlmsg_type == NLMSG_ERROR) {
        collector->stats.conntrack_dump_errors++;
        collector->stats.degraded = true;
        collector->dump_active = false;
        return;
    }
    if ((header->nlmsg_type >> 8) != NFNL_SUBSYS_CTNETLINK) {
        return;
    }
    command = (uint8_t)(header->nlmsg_type & 0xffU);
    if (dump_message && command == IPCTNL_MSG_CT_NEW) {
        tm_collector_account(collector, header, false, false);
    } else if (command == IPCTNL_MSG_CT_NEW) {
        collector->stats.conntrack_new_events++;
        tm_collector_account(collector, header, true, false);
    } else if (command == IPCTNL_MSG_CT_DELETE) {
        collector->stats.conntrack_destroy_events++;
        tm_collector_account(collector, header, false, true);
    }
}

bool tm_collector_init(tm_collector *collector, tm_aggregator *aggregator,
                       const tm_subnet_set *local_networks,
                       const tm_protocol_db *protocols, tm_identity *identity, tm_journal *journal,
                       size_t receive_buffer, unsigned int refresh_seconds)
{
    struct sockaddr_nl address;
    int actual_buffer = 0;
    int requested_buffer;
    socklen_t actual_length = sizeof(actual_buffer);
    int flags;
    if (collector == NULL || aggregator == NULL || local_networks == NULL || protocols == NULL ||
        local_networks->count == 0) {
        return false;
    }
    memset(collector, 0, sizeof(*collector));
    collector->socket_fd = -1;
    collector->aggregator = aggregator;
    collector->local_networks = local_networks;
    collector->protocols = protocols;
    collector->identity = identity;
    collector->journal = journal;
    collector->refresh_seconds = refresh_seconds == 0 ? 30 : refresh_seconds;
    if (receive_buffer == 0) {
        receive_buffer = TM_COLLECTOR_BUFFER_DEFAULT;
    }
    requested_buffer = (int)receive_buffer;
    collector->socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
    if (collector->socket_fd < 0) {
        fprintf(stderr, "trafficmon: netlink socket failed: %s\n", strerror(errno));
        return false;
    }
    if (setsockopt(collector->socket_fd, SOL_SOCKET, SO_RCVBUF, &requested_buffer,
                   sizeof(requested_buffer)) != 0) {
        fprintf(stderr, "trafficmon: warning: requested netlink receive buffer unavailable: %s\n",
                strerror(errno));
    }
    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;
    address.nl_pid = 0;
    address.nl_groups = (1U << (NFNLGRP_CONNTRACK_NEW - 1)) |
                        (1U << (NFNLGRP_CONNTRACK_DESTROY - 1));
    if (bind(collector->socket_fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "trafficmon: netlink bind failed: %s\n", strerror(errno));
        tm_collector_destroy(collector);
        return false;
    }
    flags = fcntl(collector->socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(collector->socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        tm_collector_destroy(collector);
        return false;
    }
    if (getsockopt(collector->socket_fd, SOL_SOCKET, SO_RCVBUF, &actual_buffer,
                   &actual_length) == 0) {
        collector->stats.receive_buffer = (size_t)actual_buffer;
        if ((size_t)actual_buffer < (size_t)requested_buffer * 2U) {
            fprintf(stderr,
                    "trafficmon: warning: netlink receive buffer %d is below requested %zu\n",
                    actual_buffer, receive_buffer);
        }
    }
    collector->stats.enabled = true;
    fprintf(stderr, "trafficmon: netlink connected\n");
    if (!tm_collector_request_dump(collector)) {
        tm_collector_destroy(collector);
        return false;
    }
    return true;
}

void tm_collector_destroy(tm_collector *collector)
{
    if (collector != NULL) {
        if (collector->socket_fd >= 0) {
            (void)close(collector->socket_fd);
        }
        if (collector->stats.enabled) {
            fprintf(stderr, "trafficmon: collector stopped\n");
        }
        memset(collector, 0, sizeof(*collector));
        collector->socket_fd = -1;
    }
}

bool tm_collector_tick(tm_collector *collector)
{
    union {
        struct nlmsghdr alignment;
        uint8_t bytes[TM_COLLECTOR_PACKET_MAX];
    } buffer;
    if (collector == NULL || !collector->stats.enabled) {
        return true;
    }
    if (collector->identity != NULL) {
        time_t now = time(NULL);
        (void)tm_identity_tick(collector->identity, now);
        (void)tm_identity_refresh_neighbors(collector->identity, now);
    }
    for (;;) {
        ssize_t received = recv(collector->socket_fd, buffer.bytes, sizeof(buffer.bytes), 0);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == ENOBUFS) {
                collector->stats.netlink_enobufs++;
                collector->stats.degraded = true;
                collector->next_dump = 0;
                fprintf(stderr, "trafficmon: netlink ENOBUFS; accounting is degraded\n");
                break;
            }
            return false;
        }
        for (struct nlmsghdr *header = (struct nlmsghdr *)buffer.bytes;
             NLMSG_OK(header, (unsigned int)received);
             header = NLMSG_NEXT(header, received)) {
            tm_collector_message(collector, header);
        }
    }
    if (!collector->dump_active && time(NULL) >= collector->next_dump) {
        (void)tm_collector_request_dump(collector);
    }
    return true;
}

const char *tm_collector_state(const tm_collector *collector)
{
    if (collector == NULL || !collector->stats.enabled) {
        return "disabled";
    }
    return collector->stats.degraded ? "degraded" : "running";
}

#else

bool tm_collector_init(tm_collector *collector, tm_aggregator *aggregator,
                       const tm_subnet_set *local_networks,
                       const tm_protocol_db *protocols, tm_identity *identity, tm_journal *journal,
                       size_t receive_buffer, unsigned int refresh_seconds)
{
    (void)aggregator;
    (void)local_networks;
    (void)protocols;
    (void)identity;
    (void)journal;
    (void)receive_buffer;
    (void)refresh_seconds;
    if (collector != NULL) {
        memset(collector, 0, sizeof(*collector));
        collector->socket_fd = -1;
    }
    return false;
}

void tm_collector_destroy(tm_collector *collector)
{
    if (collector != NULL) {
        memset(collector, 0, sizeof(*collector));
        collector->socket_fd = -1;
    }
}

bool tm_collector_tick(tm_collector *collector)
{
    (void)collector;
    return true;
}

const char *tm_collector_state(const tm_collector *collector)
{
    (void)collector;
    return "disabled";
}

#endif
