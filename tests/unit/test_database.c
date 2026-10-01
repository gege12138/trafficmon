#include "aggregator.h"
#include "database.h"
#include "protocol_db.h"
#include "storage.h"
#include "subnet.h"
#include "test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t test_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void test_put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void refresh_checksums(uint8_t *data, size_t length)
{
    test_put_u32(data + 32,
                 test_crc32(data + TM_DATABASE_HEADER_SIZE, length - TM_DATABASE_HEADER_SIZE));
    test_put_u32(data + 36, 0);
    test_put_u32(data + 36, test_crc32(data, TM_DATABASE_HEADER_SIZE));
}

static tm_flow_delta database_delta(uint8_t family, const char *ip, bool mac_known,
                                    uint8_t mac_marker, uint64_t value)
{
    tm_flow_delta delta;
    memset(&delta, 0, sizeof(delta));
    delta.family = family;
    delta.mac_known = mac_known;
    delta.mac[5] = mac_marker;
    delta.new_connection = true;
    delta.rx_bytes = value;
    delta.rx_packets = UINT32_MAX;
    delta.tx_bytes = value == UINT64_MAX ? UINT64_MAX : value + 1;
    delta.tx_packets = value == 0 ? 1 : value;
    TM_EXPECT_TRUE(tm_ip_parse(family, ip, &delta.host_ip));
    return delta;
}

static void expect_database_rejected(const uint8_t *data, size_t length)
{
    tm_aggregator aggregator;
    tm_database_metadata metadata;
    tm_database_result result = tm_database_decode(data, length, 10, &aggregator, &metadata);
    TM_EXPECT_TRUE(result != TM_DATABASE_OK);
    if (result == TM_DATABASE_OK) {
        tm_aggregator_destroy(&aggregator);
    }
}

void test_database(void)
{
    tm_aggregator original;
    tm_aggregator restored;
    tm_database_metadata metadata = {
        .period = {{2026, 9, 1}, {2026, 10, 1}},
        .protocol_fingerprint = UINT64_C(0x123456789abcdef0),
    };
    tm_database_metadata loaded_metadata;
    tm_database_buffer encoded;
    tm_flow_delta ipv4 = database_delta(TM_FAMILY_IPV4, "192.168.1.10", true, 10,
                                        UINT64_MAX - 1);
    tm_flow_delta ipv6 = database_delta(TM_FAMILY_IPV6, "2001:db8::42", false, 99, 0);
    char path[TM_PATH_MAX] = TM_TEST_BINARY_DIR "/database-test.db";
    uint8_t *mutated;

    {
        tm_aggregator empty;
        tm_aggregator empty_restored;
        tm_database_buffer empty_encoded;
        tm_database_metadata empty_metadata;
        TM_EXPECT_TRUE(tm_aggregator_init(&empty, 5));
        TM_EXPECT_EQ_U64(TM_DATABASE_OK, tm_database_encode(&empty, &metadata, &empty_encoded));
        TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                         tm_database_decode(empty_encoded.data, empty_encoded.length, 5,
                                            &empty_restored, &empty_metadata));
        TM_EXPECT_EQ_U64(0, empty_restored.count);
        tm_aggregator_destroy(&empty_restored);
        tm_database_buffer_destroy(&empty_encoded);
        tm_aggregator_destroy(&empty);
    }

    remove(path);
    {
        char temp_path[TM_PATH_MAX] = TM_TEST_BINARY_DIR "/database-test.db.tmp";
        remove(temp_path);
    }
    TM_EXPECT_TRUE(tm_aggregator_init(&original, 2));
    TM_EXPECT_TRUE(tm_aggregator_add(&original, &ipv4, 1));
    TM_EXPECT_TRUE(tm_aggregator_add(&original, &ipv6, TM_PROTOCOL_OTHER));
    TM_EXPECT_EQ_U64(TM_DATABASE_OK, tm_database_encode(&original, &metadata, &encoded));
    TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                     tm_database_decode(encoded.data, encoded.length, 2, &restored,
                                        &loaded_metadata));
    TM_EXPECT_EQ_U64(2, restored.count);
    TM_EXPECT_EQ_U64(tm_date_encode(metadata.period.start),
                     tm_date_encode(loaded_metadata.period.start));
    TM_EXPECT_EQ_U64(metadata.protocol_fingerprint, loaded_metadata.protocol_fingerprint);
    tm_aggregator_destroy(&restored);

    TM_EXPECT_EQ_U64(TM_DATABASE_LIMIT_EXCEEDED,
                     tm_database_decode(encoded.data, encoded.length, 1, &restored,
                                        &loaded_metadata));
    TM_EXPECT_TRUE(tm_storage_commit_database(path, &original, &metadata));
    TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                     tm_storage_load_database(path, 2, &restored, &loaded_metadata));
    TM_EXPECT_EQ_U64(2, restored.count);
    ipv4.new_connection = false;
    ipv4.rx_bytes = 1;
    TM_EXPECT_TRUE(tm_aggregator_add(&restored, &ipv4, 1));
    {
        tm_flow_delta rejected = database_delta(TM_FAMILY_IPV4, "192.168.1.99", true, 99, 1);
        TM_EXPECT_TRUE(!tm_aggregator_add(&restored, &rejected, 2));
        TM_EXPECT_EQ_U64(1, restored.dropped_new_keys);
    }
    tm_aggregator_destroy(&restored);

    expect_database_rejected(encoded.data, 0);
    expect_database_rejected(encoded.data, TM_DATABASE_HEADER_SIZE - 1);
    expect_database_rejected(encoded.data, encoded.length - 1);
    mutated = malloc(encoded.length + 1);
    TM_EXPECT_TRUE(mutated != NULL);
    if (mutated != NULL) {
        memcpy(mutated, encoded.data, encoded.length);
        mutated[0] ^= 1;
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        mutated[8] = 99;
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        mutated[10] = 1;
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        mutated[12] = 1;
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        memset(mutated + 40, 0, 8);
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        test_put_u32(mutated + 16, TM_DATABASE_HARD_RECORD_LIMIT + 1U);
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        mutated[TM_DATABASE_HEADER_SIZE] = 99;
        refresh_checksums(mutated, encoded.length);
        expect_database_rejected(mutated, encoded.length);

        memcpy(mutated, encoded.data, encoded.length);
        mutated[encoded.length] = 0;
        expect_database_rejected(mutated, encoded.length + 1);

        for (size_t index = 0; index < encoded.length; index++) {
            mutated[index] = (uint8_t)(index * 37U + 11U);
        }
        expect_database_rejected(mutated, encoded.length);
        free(mutated);
    }

    /* A completed temp write cannot damage the old valid database before replacement. */
    tm_aggregator_destroy(&original);
    TM_EXPECT_TRUE(tm_aggregator_init(&original, 2));
    TM_EXPECT_TRUE(tm_aggregator_add(&original, &ipv4, 1));
    tm_database_buffer_destroy(&encoded);
    TM_EXPECT_EQ_U64(TM_DATABASE_OK, tm_database_encode(&original, &metadata, &encoded));
    TM_EXPECT_TRUE(tm_storage_write_temp(path, encoded.data, encoded.length));
    TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                     tm_storage_load_database(path, 2, &restored, &loaded_metadata));
    TM_EXPECT_EQ_U64(2, restored.count);
    tm_aggregator_destroy(&restored);
    TM_EXPECT_TRUE(tm_storage_replace_temp(path));
    TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                     tm_storage_load_database(path, 2, &restored, &loaded_metadata));
    TM_EXPECT_EQ_U64(1, restored.count);
    tm_aggregator_destroy(&restored);

    tm_database_buffer_destroy(&encoded);
    tm_aggregator_destroy(&original);
    remove(path);

    {
        const uint64_t extremes[] = {0, 1, UINT32_MAX, UINT64_MAX - 1, UINT64_MAX};
        tm_aggregator extreme_source;
        tm_aggregator extreme_restored;
        tm_database_buffer extreme_encoded;
        tm_database_metadata extreme_metadata;
        TM_EXPECT_TRUE(tm_aggregator_init(&extreme_source, 5));
        for (uint8_t index = 0; index < 5; index++) {
            tm_flow_delta delta = database_delta(TM_FAMILY_IPV4, "198.51.100.1", index != 2,
                                                 index, extremes[index]);
            delta.host_ip.bytes[3] = (uint8_t)(index + 1);
            TM_EXPECT_TRUE(tm_aggregator_add(&extreme_source, &delta, index));
        }
        TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                         tm_database_encode(&extreme_source, &metadata, &extreme_encoded));
        TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                         tm_database_decode(extreme_encoded.data, extreme_encoded.length, 5,
                                            &extreme_restored, &extreme_metadata));
        TM_EXPECT_EQ_U64(5, extreme_restored.count);
        for (uint8_t index = 0; index < 5; index++) {
            tm_aggregate_key key;
            const tm_aggregate_entry *entry;
            memset(&key, 0, sizeof(key));
            key.family = TM_FAMILY_IPV4;
            TM_EXPECT_TRUE(tm_ip_parse(key.family, "198.51.100.1", &key.host_ip));
            key.host_ip.bytes[3] = (uint8_t)(index + 1);
            key.mac_known = index != 2;
            key.mac[5] = key.mac_known ? index : 0;
            key.protocol_id = index;
            entry = tm_aggregator_find(&extreme_restored, &key);
            TM_EXPECT_TRUE(entry != NULL);
            if (entry != NULL) {
                TM_EXPECT_EQ_U64(extremes[index], entry->rx_bytes);
            }
        }
        tm_aggregator_destroy(&extreme_restored);
        tm_database_buffer_destroy(&extreme_encoded);
        tm_aggregator_destroy(&extreme_source);
    }
}
