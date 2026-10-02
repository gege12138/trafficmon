#include "journal/journal.h"
#include "storage.h"
#include "subnet.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

static bool make_event(tm_journal_event *event, int64_t timestamp, unsigned int marker)
{
    memset(event, 0, sizeof(*event));
    event->timestamp = timestamp;
    event->family = TM_FAMILY_IPV4;
    event->protocol = 6;
    event->src_port = (uint16_t)(1000U + marker);
    event->dst_port = 443;
    event->rx_bytes = marker * 100U;
    event->tx_bytes = marker * 10U;
    (void)snprintf(event->device_id, sizeof(event->device_id), "device-%u", marker);
    return tm_ip_parse(event->family, "192.168.1.10", &event->src_ip) &&
           tm_ip_parse(event->family, "203.0.113.20", &event->dst_ip);
}

static bool append_bytes(const char *path, const char *bytes, size_t length)
{
    FILE *stream = fopen(path, "ab");
    bool success = stream != NULL && fwrite(bytes, 1, length, stream) == length;
    if (stream != NULL && fclose(stream) != 0) {
        success = false;
    }
    return success;
}

static bool write_bytes(const char *path, const char *bytes, size_t length)
{
    FILE *stream = fopen(path, "wb");
    bool success = stream != NULL && fwrite(bytes, 1, length, stream) == length;
    if (stream != NULL && fclose(stream) != 0) {
        success = false;
    }
    return success;
}

void test_journal(void)
{
    const char *directory = TM_TEST_BINARY_DIR "/journal-test";
    const int64_t timestamp = INT64_C(1788220800); /* 2026-09-01 00:00:00 UTC */
    const int64_t bad_timestamp = INT64_C(1788307200);
    tm_journal journal;
    tm_journal_event expected[4];
    tm_journal_event actual[8];
    char path[TM_PATH_MAX];
    char bad_path[TM_PATH_MAX];
    size_t count = 0;

    {
        tm_raw_flow flow;
        tm_flow_delta delta;
        tm_journal_event converted;
        memset(&flow, 0, sizeof(flow));
        memset(&delta, 0, sizeof(delta));
        flow.family = TM_FAMILY_IPV4;
        flow.l4_proto = 17;
        flow.orig_src_port = 53000;
        flow.orig_dst_port = 53;
        delta.family = TM_FAMILY_IPV4;
        delta.mac_known = true;
        delta.mac[0] = 0x02;
        delta.mac[5] = 0x05;
        delta.rx_bytes = 200;
        delta.tx_bytes = 100;
        TM_EXPECT_TRUE(tm_ip_parse(flow.family, "192.168.1.5", &flow.orig_src));
        TM_EXPECT_TRUE(tm_ip_parse(flow.family, "8.8.8.8", &flow.orig_dst));
        delta.host_ip = flow.orig_src;
        TM_EXPECT_TRUE(tm_journal_event_from_flow(&flow, &delta, (time_t)timestamp,
                                                   &converted));
        TM_EXPECT_EQ_U64(17, converted.protocol);
        TM_EXPECT_EQ_U64(53000, converted.src_port);
        TM_EXPECT_EQ_U64(200, converted.rx_bytes);
        TM_EXPECT_TRUE(strcmp("02:00:00:00:00:05", converted.device_id) == 0);
    }

    TM_EXPECT_TRUE(tm_storage_ensure_directory(directory));
    TM_EXPECT_TRUE(tm_journal_shard_path(directory, timestamp, path));
    TM_EXPECT_TRUE(tm_journal_shard_path(directory, bad_timestamp, bad_path));
    (void)remove(path);
    (void)remove(bad_path);

    TM_EXPECT_TRUE(make_event(&expected[0], timestamp + 1, 1));
    TM_EXPECT_TRUE(make_event(&expected[1], timestamp + 2, 2));
    TM_EXPECT_TRUE(make_event(&expected[2], timestamp + 3, 3));
    TM_EXPECT_TRUE(tm_journal_init(&journal, directory));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &expected[0]));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &expected[1]));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &expected[2]));
    TM_EXPECT_TRUE(tm_journal_flush(&journal));
    tm_journal_destroy(&journal);

    TM_EXPECT_TRUE(tm_journal_read_file(path, actual, 8, &count));
    TM_EXPECT_EQ_U64(3, count);
    for (size_t index = 0; index < count; index++) {
        TM_EXPECT_EQ_U64(expected[index].timestamp, actual[index].timestamp);
        TM_EXPECT_EQ_U64(expected[index].src_port, actual[index].src_port);
        TM_EXPECT_EQ_U64(expected[index].rx_bytes, actual[index].rx_bytes);
        TM_EXPECT_EQ_U64(expected[index].tx_bytes, actual[index].tx_bytes);
        TM_EXPECT_TRUE(strcmp(expected[index].device_id, actual[index].device_id) == 0);
        TM_EXPECT_MEMORY_EQ(expected[index].src_ip.bytes, actual[index].src_ip.bytes,
                            TM_IPV6_LENGTH);
        TM_EXPECT_MEMORY_EQ(expected[index].dst_ip.bytes, actual[index].dst_ip.bytes,
                            TM_IPV6_LENGTH);
    }

    /* Simulate a crash during the final append. Recovery truncates the partial tail. */
    TM_EXPECT_TRUE(append_bytes(path, "partial", 7));
    TM_EXPECT_TRUE(!tm_journal_read_file(path, actual, 8, &count));
    TM_EXPECT_TRUE(make_event(&expected[3], timestamp + 4, 4));
    TM_EXPECT_TRUE(tm_journal_init(&journal, directory));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &expected[3]));
    TM_EXPECT_TRUE(tm_journal_flush(&journal));
    tm_journal_destroy(&journal);
    TM_EXPECT_TRUE(tm_journal_read_file(path, actual, 8, &count));
    TM_EXPECT_EQ_U64(4, count);
    for (size_t index = 0; index < count; index++) {
        TM_EXPECT_EQ_U64(index + 1, actual[index].src_port - 1000U);
    }

    /* An unrelated/malformed shard is rejected; failure stays local to the journal. */
    TM_EXPECT_TRUE(write_bytes(bad_path, "not-a-journal", 13));
    TM_EXPECT_TRUE(make_event(&expected[0], bad_timestamp + 1, 5));
    TM_EXPECT_TRUE(tm_journal_init(&journal, directory));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &expected[0]));
    TM_EXPECT_TRUE(!tm_journal_flush(&journal));
    TM_EXPECT_EQ_U64(0, journal.queue_count);
    tm_journal_destroy(&journal);
    TM_EXPECT_TRUE(!tm_journal_read_file(bad_path, actual, 8, &count));

    (void)remove(path);
    (void)remove(bad_path);
}
