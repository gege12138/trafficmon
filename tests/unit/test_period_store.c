#include "period_store.h"
#include "subnet.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_period_store_files(const char *directory)
{
    static const char *const names[] = {
        "current.db",          "current.db.tmp",       "period-20260101.db",
        "period-20260201.db",  "period-20260301.db",   "period-20260401.db",
        "period-20260501.db",  "period-20200101.db",   "period-garbage.db",
        "notes.txt",
    };
    char path[TM_PATH_MAX];
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        (void)snprintf(path, sizeof(path), "%s/%s", directory, names[index]);
        remove(path);
    }
}

static bool add_period_record(tm_period_store *store, uint8_t marker)
{
    tm_flow_delta delta;
    memset(&delta, 0, sizeof(delta));
    delta.family = TM_FAMILY_IPV4;
    delta.mac_known = true;
    delta.mac[5] = marker;
    delta.new_connection = true;
    delta.rx_bytes = marker;
    if (!tm_ip_parse(delta.family, "192.168.1.10", &delta.host_ip)) {
        return false;
    }
    delta.host_ip.bytes[3] = marker;
    return tm_aggregator_add(&store->aggregator, &delta, 1);
}

void test_period_store(void)
{
    const char *directory = TM_TEST_BINARY_DIR "/period-store-test";
    tm_period_rule rule;
    tm_period_store store;
    tm_period_descriptor *periods;
    size_t period_count;
    bool rolled_over;
    char malformed_path[TM_PATH_MAX];
    FILE *malformed;
    char unrelated_path[TM_PATH_MAX];
    FILE *unrelated;
    tm_aggregator historical;
    tm_database_metadata historical_metadata;

    TM_EXPECT_TRUE(tm_storage_ensure_directory(directory));
    cleanup_period_store_files(directory);
    (void)snprintf(malformed_path, sizeof(malformed_path), "%s/period-20200101.db", directory);
    malformed = fopen(malformed_path, "wb");
    TM_EXPECT_TRUE(malformed != NULL);
    if (malformed != NULL) {
        TM_EXPECT_TRUE(fwrite("bad", 1, 3, malformed) == 3);
        TM_EXPECT_TRUE(fclose(malformed) == 0);
    }
    (void)snprintf(unrelated_path, sizeof(unrelated_path), "%s/notes.txt", directory);
    unrelated = fopen(unrelated_path, "wb");
    TM_EXPECT_TRUE(unrelated != NULL);
    if (unrelated != NULL) {
        TM_EXPECT_TRUE(fwrite("keep", 1, 4, unrelated) == 4);
        TM_EXPECT_TRUE(fclose(unrelated) == 0);
    }

    TM_EXPECT_TRUE(tm_period_rule_parse("1", &rule));
    TM_EXPECT_TRUE(tm_period_store_init(&store, directory, &rule, (tm_date){2026, 1, 31}, 10, 3,
                                        UINT64_C(0x1111222233334444)));
    TM_EXPECT_TRUE(add_period_record(&store, 1));
    TM_EXPECT_TRUE(tm_period_store_commit(&store));
    TM_EXPECT_TRUE(tm_period_store_rollover(&store, (tm_date){2026, 1, 31}, &rolled_over));
    TM_EXPECT_TRUE(!rolled_over);

    for (uint8_t month = 2; month <= 5; month++) {
        TM_EXPECT_TRUE(tm_period_store_rollover(&store, (tm_date){2026, month, 1}, &rolled_over));
        TM_EXPECT_TRUE(rolled_over);
        TM_EXPECT_EQ_U64(0, store.aggregator.count);
        TM_EXPECT_TRUE(add_period_record(&store, month));
        TM_EXPECT_TRUE(tm_period_store_commit(&store));
    }

    TM_EXPECT_TRUE(tm_period_store_list(&store, &periods, &period_count));
    TM_EXPECT_EQ_U64(4, period_count);
    if (period_count == 4) {
        TM_EXPECT_TRUE(periods[0].current);
        TM_EXPECT_EQ_U64(20260501, tm_date_encode(periods[0].period.start));
        TM_EXPECT_EQ_U64(20260401, tm_date_encode(periods[1].period.start));
        TM_EXPECT_EQ_U64(20260301, tm_date_encode(periods[2].period.start));
        TM_EXPECT_EQ_U64(20260201, tm_date_encode(periods[3].period.start));
    }
    free(periods);
    TM_EXPECT_TRUE(tm_period_store_rollover(&store, (tm_date){2026, 4, 15}, &rolled_over));
    TM_EXPECT_TRUE(!rolled_over);
    TM_EXPECT_EQ_U64(20260501, tm_date_encode(store.metadata.period.start));
    TM_EXPECT_TRUE(tm_storage_file_exists(malformed_path));
    TM_EXPECT_TRUE(tm_storage_file_exists(unrelated_path));

    TM_EXPECT_EQ_U64(TM_DATABASE_OK,
                     tm_period_store_load(&store, (tm_date){2026, 3, 1}, &historical,
                                          &historical_metadata));
    TM_EXPECT_EQ_U64(1, historical.count);
    tm_aggregator_destroy(&historical);

    tm_period_store_destroy(&store);
    TM_EXPECT_TRUE(!tm_period_store_init(&store, directory, &rule, (tm_date){2026, 5, 15}, 10, 3,
                                         UINT64_C(0x9999aaaabbbbcccc)));
    TM_EXPECT_TRUE(tm_period_store_init(&store, directory, &rule, (tm_date){2026, 5, 15}, 10, 3,
                                        UINT64_C(0x1111222233334444)));
    TM_EXPECT_EQ_U64(1, store.aggregator.count);
    tm_period_store_destroy(&store);
    cleanup_period_store_files(directory);
}
