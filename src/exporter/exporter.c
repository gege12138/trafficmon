#include "exporter/exporter.h"

#include "journal/journal.h"
#include "storage.h"
#include "subnet.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TM_EXPORT_FILENO _fileno
#define TM_EXPORT_FSYNC _commit
#else
#include <unistd.h>
#define TM_EXPORT_FILENO fileno
#define TM_EXPORT_FSYNC fsync
#endif

typedef struct {
    FILE *stream;
} tm_export_context;

static bool tm_export_next_date(tm_date *date)
{
    tm_date next;
    if (date == NULL || !tm_date_is_valid(*date) || date->year >= 9999) {
        return false;
    }
    next = *date;
    next.day++;
    if (!tm_date_is_valid(next)) {
        next.day = 1;
        next.month++;
        if (next.month > 12) {
            next.month = 1;
            next.year++;
        }
    }
    if (!tm_date_is_valid(next)) {
        return false;
    }
    *date = next;
    return true;
}

static bool tm_export_shard_path(const char *directory, tm_date date,
                                 char output[TM_PATH_MAX])
{
    int written;
    if (directory == NULL || directory[0] == '\0' || !tm_date_is_valid(date) ||
        date.year > 9999) {
        return false;
    }
    written = snprintf(output, TM_PATH_MAX, "%s/journal-%04d%02u%02u.bin", directory,
                       date.year, date.month, date.day);
    return written > 0 && written < TM_PATH_MAX;
}

static bool tm_export_csv_field(FILE *stream, const char *text)
{
    bool quote = false;
    const char *cursor;
    for (cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == ',' || *cursor == '"' || *cursor == '\r' || *cursor == '\n') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return fputs(text, stream) >= 0;
    }
    if (fputc('"', stream) == EOF) {
        return false;
    }
    for (cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '"' && fputc('"', stream) == EOF) {
            return false;
        }
        if (fputc((unsigned char)*cursor, stream) == EOF) {
            return false;
        }
    }
    return fputc('"', stream) != EOF;
}

static bool tm_export_event(const tm_journal_event *event, void *opaque)
{
    tm_export_context *context = opaque;
    char source[46];
    char destination[46];
    if (!tm_ip_format(event->family, &event->src_ip, source, sizeof(source)) ||
        !tm_ip_format(event->family, &event->dst_ip, destination, sizeof(destination)) ||
        fprintf(context->stream, "%" PRId64 ",", event->timestamp) < 0 ||
        !tm_export_csv_field(context->stream, event->device_id) ||
        fputc(',', context->stream) == EOF ||
        !tm_export_csv_field(context->stream, source) ||
        fputc(',', context->stream) == EOF ||
        !tm_export_csv_field(context->stream, destination) ||
        fprintf(context->stream, ",%u,%u,%u,%" PRIu64 ",%" PRIu64 "\r\n",
                event->src_port, event->dst_port, event->protocol,
                event->rx_bytes, event->tx_bytes) < 0) {
        return false;
    }
    return true;
}

bool tm_export_csv(const tm_export_options *options, tm_export_stats *stats)
{
    static const char header[] =
        "timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes\r\n";
    tm_export_stats result_stats;
    tm_export_context context;
    tm_date date;
    char shard_path[TM_PATH_MAX];
    char temp_path[TM_PATH_MAX];
    int written;
    bool success = false;
    bool stream_success;
    memset(&result_stats, 0, sizeof(result_stats));
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (options == NULL || options->journal_directory == NULL ||
        options->output_path == NULL || options->journal_directory[0] == '\0' ||
        options->output_path[0] == '\0' || !tm_date_is_valid(options->start_date) ||
        !tm_date_is_valid(options->end_date) || options->start_date.year > 9999 ||
        options->end_date.year > 9999 ||
        tm_date_encode(options->start_date) > tm_date_encode(options->end_date)) {
        return false;
    }
    written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", options->output_path);
    if (written <= 0 || written >= (int)sizeof(temp_path)) {
        return false;
    }
    context.stream = fopen(temp_path, "wb");
    if (context.stream == NULL || fwrite(header, 1, sizeof(header) - 1, context.stream) !=
                                      sizeof(header) - 1) {
        if (context.stream != NULL) {
            (void)fclose(context.stream);
        }
        (void)remove(temp_path);
        return false;
    }
    date = options->start_date;
    for (;;) {
        tm_journal_scan_stats scan_stats;
        tm_journal_scan_result scan_result;
        if (!tm_export_shard_path(options->journal_directory, date, shard_path)) {
            goto cleanup;
        }
        scan_result = tm_journal_scan_file(shard_path, tm_export_event, &context, &scan_stats);
        result_stats.exported_records += scan_stats.valid_records;
        result_stats.corrupt_records += scan_stats.corrupt_records;
        if (scan_result == TM_JOURNAL_SCAN_NOT_FOUND) {
            result_stats.missing_files++;
        } else if (scan_result == TM_JOURNAL_SCAN_INVALID_HEADER) {
            result_stats.invalid_files++;
            fprintf(stderr, "trafficexport: invalid journal header skipped: %s\n", shard_path);
        } else if (scan_result == TM_JOURNAL_SCAN_IO_ERROR) {
            fprintf(stderr, "trafficexport: unable to read journal: %s: %s\n", shard_path,
                    strerror(errno));
            goto cleanup;
        } else if (scan_result == TM_JOURNAL_SCAN_VISITOR_ERROR) {
            fprintf(stderr, "trafficexport: unable to write CSV output\n");
            goto cleanup;
        } else {
            result_stats.files_read++;
        }
        if (scan_stats.corrupt_records != 0) {
            fprintf(stderr,
                    "trafficexport: skipped %" PRIu64 " corrupt record(s) in %s\n",
                    scan_stats.corrupt_records, shard_path);
        }
        if (tm_date_encode(date) == tm_date_encode(options->end_date)) {
            break;
        }
        if (!tm_export_next_date(&date)) {
            goto cleanup;
        }
    }
    stream_success = fflush(context.stream) == 0 &&
                     TM_EXPORT_FSYNC(TM_EXPORT_FILENO(context.stream)) == 0;
    if (fclose(context.stream) != 0) {
        stream_success = false;
    }
    context.stream = NULL;
    if (!stream_success) {
        goto cleanup_file;
    }
    if (!tm_storage_replace_temp(options->output_path)) {
        goto cleanup_file;
    }
    success = true;
    if (stats != NULL) {
        *stats = result_stats;
    }

cleanup_file:
    if (!success) {
        (void)remove(temp_path);
    }
    return success;

cleanup:
    (void)fclose(context.stream);
    context.stream = NULL;
    goto cleanup_file;
}
