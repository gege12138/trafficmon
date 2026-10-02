#include "retention/retention.h"
#include "storage.h"
#include "test.h"

#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define tm_test_rmdir _rmdir
#else
#include <unistd.h>
#define tm_test_rmdir rmdir
#endif

static bool tm_retention_test_write(const char *path)
{
    FILE *stream = fopen(path, "wb");
    return stream != NULL && fclose(stream) == 0;
}

void test_retention(void)
{
    const char *directory = TM_TEST_BINARY_DIR "/retention-test";
    const char *empty_directory = TM_TEST_BINARY_DIR "/retention-empty-test";
    const tm_date today = {2026, 5, 1};
    const char *names[] = {
        "journal-20251231.bin", /* 121 days old: delete. */
        "journal-20260101.bin", /* Exactly 120 days old: retain. */
        "journal-20260430.bin", /* Not expired: retain. */
        "journal-20260230.bin", /* Invalid calendar date: ignore. */
        "journal-20260101.bin.extra", /* Invalid suffix: ignore. */
    };
    char paths[sizeof(names) / sizeof(names[0])][TM_PATH_MAX];
    const char *failure_directory =
        TM_TEST_BINARY_DIR "/retention-test/journal-20251230.bin";
    const char *failure_child =
        TM_TEST_BINARY_DIR "/retention-test/journal-20251230.bin/child";
    tm_retention_stats stats;
    size_t index;

    TM_EXPECT_TRUE(tm_storage_ensure_directory(directory));
    TM_EXPECT_TRUE(tm_storage_ensure_directory(empty_directory));
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        (void)snprintf(paths[index], sizeof(paths[index]), "%s/%s", directory, names[index]);
        (void)remove(paths[index]);
    }
    (void)remove(failure_child);
    (void)tm_test_rmdir(failure_directory);

    /* Empty directories are valid and require no special handling. */
    TM_EXPECT_TRUE(tm_retention_cleanup(empty_directory, 120, today, &stats));
    TM_EXPECT_EQ_U64(0, stats.deleted_files);
    TM_EXPECT_EQ_U64(0, stats.delete_failures);

    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        TM_EXPECT_TRUE(tm_retention_test_write(paths[index]));
    }
    TM_EXPECT_TRUE(tm_retention_cleanup(directory, 120, today, &stats));
    TM_EXPECT_EQ_U64(3, stats.journal_files);
    TM_EXPECT_EQ_U64(1, stats.deleted_files);
    TM_EXPECT_EQ_U64(2, stats.retained_files);
    TM_EXPECT_EQ_U64(2, stats.ignored_entries);
    TM_EXPECT_TRUE(fopen(paths[0], "rb") == NULL);
    {
        FILE *boundary = fopen(paths[1], "rb");
        FILE *recent = fopen(paths[2], "rb");
        TM_EXPECT_TRUE(boundary != NULL);
        TM_EXPECT_TRUE(recent != NULL);
        if (boundary != NULL) {
            (void)fclose(boundary);
        }
        if (recent != NULL) {
            (void)fclose(recent);
        }
    }

    /* A non-empty directory with a valid shard name deterministically exercises delete failure. */
    TM_EXPECT_TRUE(tm_storage_ensure_directory(failure_directory));
    TM_EXPECT_TRUE(tm_retention_test_write(failure_child));
    TM_EXPECT_TRUE(!tm_retention_cleanup(directory, 120, today, &stats));
    TM_EXPECT_EQ_U64(1, stats.delete_failures);

    (void)remove(failure_child);
    (void)tm_test_rmdir(failure_directory);
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        (void)remove(paths[index]);
    }
    (void)tm_test_rmdir(directory);
    (void)tm_test_rmdir(empty_directory);
}
