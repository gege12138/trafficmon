#include "exporter/exporter.h"
#include "journal/journal.h"
#include "storage.h"
#include "subnet.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TM_EXPORT_STRESS_RECORDS = 1000000, TM_EXPORT_FIXTURE_BATCH = 1024 };

static bool make_export_event(tm_journal_event *event, int64_t timestamp,
                              unsigned int marker, const char *device_id)
{
    memset(event, 0, sizeof(*event));
    event->timestamp = timestamp;
    event->family = TM_FAMILY_IPV4;
    event->protocol = 6;
    event->src_port = (uint16_t)(10000U + marker);
    event->dst_port = 443;
    event->rx_bytes = marker * 100U;
    event->tx_bytes = marker * 10U;
    (void)snprintf(event->device_id, sizeof(event->device_id), "%s", device_id);
    return tm_ip_parse(event->family, "192.168.1.10", &event->src_ip) &&
           tm_ip_parse(event->family, "203.0.113.20", &event->dst_ip);
}

static bool copy_header(const char *source_path, const char *destination_path)
{
    uint8_t header[TM_JOURNAL_HEADER_SIZE];
    FILE *source = fopen(source_path, "rb");
    FILE *destination;
    bool success = source != NULL && fread(header, 1, sizeof(header), source) == sizeof(header);
    if (source != NULL && fclose(source) != 0) {
        success = false;
    }
    if (!success) {
        return false;
    }
    destination = fopen(destination_path, "wb");
    success = destination != NULL &&
              fwrite(header, 1, sizeof(header), destination) == sizeof(header);
    if (destination != NULL && fclose(destination) != 0) {
        success = false;
    }
    return success;
}

static bool corrupt_second_record(const char *path)
{
    FILE *stream = fopen(path, "r+b");
    long offset = TM_JOURNAL_HEADER_SIZE + TM_JOURNAL_RECORD_SIZE + 32;
    int value;
    bool success = stream != NULL && fseek(stream, offset, SEEK_SET) == 0 &&
                   (value = fgetc(stream)) != EOF && fseek(stream, offset, SEEK_SET) == 0 &&
                   fputc(value ^ 0x80, stream) != EOF;
    if (stream != NULL && fclose(stream) != 0) {
        success = false;
    }
    return success;
}

static bool read_small_file(const char *path, char *output, size_t capacity)
{
    FILE *stream = fopen(path, "rb");
    size_t amount;
    bool success;
    if (stream == NULL || capacity == 0) {
        return false;
    }
    amount = fread(output, 1, capacity - 1, stream);
    output[amount] = '\0';
    success = feof(stream) && fclose(stream) == 0;
    return success;
}

static bool create_stress_shard(const char *seed_path, const char *stress_path)
{
    uint8_t header[TM_JOURNAL_HEADER_SIZE];
    uint8_t record[TM_JOURNAL_RECORD_SIZE];
    uint8_t *batch;
    FILE *source = fopen(seed_path, "rb");
    FILE *destination;
    size_t remaining = TM_EXPORT_STRESS_RECORDS;
    bool success = source != NULL && fread(header, 1, sizeof(header), source) == sizeof(header) &&
                   fread(record, 1, sizeof(record), source) == sizeof(record);
    if (source != NULL && fclose(source) != 0) {
        success = false;
    }
    if (!success) {
        return false;
    }
    batch = malloc((size_t)TM_JOURNAL_RECORD_SIZE * TM_EXPORT_FIXTURE_BATCH);
    if (batch == NULL) {
        return false;
    }
    for (size_t index = 0; index < TM_EXPORT_FIXTURE_BATCH; index++) {
        memcpy(batch + index * TM_JOURNAL_RECORD_SIZE, record, TM_JOURNAL_RECORD_SIZE);
    }
    destination = fopen(stress_path, "wb");
    success = destination != NULL &&
              fwrite(header, 1, sizeof(header), destination) == sizeof(header);
    while (success && remaining != 0) {
        size_t count = remaining < TM_EXPORT_FIXTURE_BATCH ? remaining : TM_EXPORT_FIXTURE_BATCH;
        success = fwrite(batch, TM_JOURNAL_RECORD_SIZE, count, destination) == count;
        remaining -= count;
    }
    if (destination != NULL && fclose(destination) != 0) {
        success = false;
    }
    free(batch);
    return success;
}

static uint64_t count_lines(const char *path)
{
    uint8_t buffer[65536];
    FILE *stream = fopen(path, "rb");
    uint64_t lines = 0;
    if (stream == NULL) {
        return 0;
    }
    for (;;) {
        size_t amount = fread(buffer, 1, sizeof(buffer), stream);
        for (size_t index = 0; index < amount; index++) {
            if (buffer[index] == '\n') {
                lines++;
            }
        }
        if (amount != sizeof(buffer)) {
            break;
        }
    }
    (void)fclose(stream);
    return lines;
}

void test_exporter(void)
{
    const char *directory = TM_TEST_BINARY_DIR "/exporter-test";
    const int64_t september_first = INT64_C(1788220800);
    const char utf8_device[] = "\xe8\xae\xbe\xe5\xa4\x87,\"alpha\"";
    tm_journal journal;
    tm_journal_event event;
    tm_export_options options;
    tm_export_stats stats;
    char day1[TM_PATH_MAX];
    char day2[TM_PATH_MAX];
    char day3[TM_PATH_MAX];
    char day5[TM_PATH_MAX];
    char stress_shard[TM_PATH_MAX];
    char output[TM_PATH_MAX];
    char stress_output[TM_PATH_MAX];
    char contents[4096];
    FILE *empty;

    TM_EXPECT_TRUE(tm_storage_ensure_directory(directory));
    (void)snprintf(day1, sizeof(day1), "%s/journal-20260901.bin", directory);
    (void)snprintf(day2, sizeof(day2), "%s/journal-20260902.bin", directory);
    (void)snprintf(day3, sizeof(day3), "%s/journal-20260903.bin", directory);
    (void)snprintf(day5, sizeof(day5), "%s/journal-20260905.bin", directory);
    (void)snprintf(stress_shard, sizeof(stress_shard), "%s/journal-20261001.bin", directory);
    (void)snprintf(output, sizeof(output), "%s/export.csv", directory);
    (void)snprintf(stress_output, sizeof(stress_output), "%s/stress.csv", directory);
    (void)remove(day1);
    (void)remove(day2);
    (void)remove(day3);
    (void)remove(day5);
    (void)remove(stress_shard);
    (void)remove(output);
    (void)remove(stress_output);

    TM_EXPECT_TRUE(tm_journal_init(&journal, directory));
    TM_EXPECT_TRUE(make_export_event(&event, september_first + 1, 1, utf8_device));
    TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &event));
    for (unsigned int marker = 2; marker <= 4; marker++) {
        TM_EXPECT_TRUE(make_export_event(&event, september_first + 86400 + marker, marker,
                                         "device-beta"));
        TM_EXPECT_TRUE(tm_journal_enqueue(&journal, &event));
    }
    TM_EXPECT_TRUE(tm_journal_flush(&journal));
    tm_journal_destroy(&journal);
    TM_EXPECT_TRUE(copy_header(day1, day3));
    TM_EXPECT_TRUE(corrupt_second_record(day2));

    memset(&options, 0, sizeof(options));
    options.journal_directory = directory;
    options.output_path = output;
    options.start_date = (tm_date){2026, 9, 1};
    options.end_date = (tm_date){2026, 9, 1};
    TM_EXPECT_TRUE(tm_export_csv(&options, &stats));
    TM_EXPECT_EQ_U64(1, stats.exported_records);
    TM_EXPECT_TRUE(read_small_file(output, contents, sizeof(contents)));
    TM_EXPECT_TRUE(strstr(contents,
                          "timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes\r\n") ==
                   contents);
    TM_EXPECT_TRUE(strstr(contents, "\"\xe8\xae\xbe\xe5\xa4\x87,\"\"alpha\"\"\"") != NULL);

    options.end_date = (tm_date){2026, 9, 4};
    TM_EXPECT_TRUE(tm_export_csv(&options, &stats));
    TM_EXPECT_EQ_U64(3, stats.exported_records);
    TM_EXPECT_EQ_U64(1, stats.corrupt_records);
    TM_EXPECT_EQ_U64(3, stats.files_read);
    TM_EXPECT_EQ_U64(1, stats.missing_files);
    TM_EXPECT_EQ_U64(4, count_lines(output));

    empty = fopen(day5, "wb");
    TM_EXPECT_TRUE(empty != NULL);
    if (empty != NULL) {
        TM_EXPECT_TRUE(fclose(empty) == 0);
    }
    options.start_date = (tm_date){2026, 9, 5};
    options.end_date = options.start_date;
    TM_EXPECT_TRUE(tm_export_csv(&options, &stats));
    TM_EXPECT_EQ_U64(0, stats.exported_records);
    TM_EXPECT_EQ_U64(1, stats.invalid_files);
    TM_EXPECT_EQ_U64(1, count_lines(output));

    TM_EXPECT_TRUE(create_stress_shard(day1, stress_shard));
    options.output_path = stress_output;
    options.start_date = (tm_date){2026, 10, 1};
    options.end_date = options.start_date;
    TM_EXPECT_TRUE(tm_export_csv(&options, &stats));
    TM_EXPECT_EQ_U64(TM_EXPORT_STRESS_RECORDS, stats.exported_records);
    TM_EXPECT_EQ_U64(0, stats.corrupt_records);
    TM_EXPECT_EQ_U64((uint64_t)TM_EXPORT_STRESS_RECORDS + 1, count_lines(stress_output));

    (void)remove(day1);
    (void)remove(day2);
    (void)remove(day3);
    (void)remove(day5);
    (void)remove(stress_shard);
    (void)remove(output);
    (void)remove(stress_output);
}
