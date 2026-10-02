#include "exporter/exporter.h"
#include "version.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void tm_export_usage(void)
{
    fprintf(stderr,
            "usage: trafficexport [--journal-dir DIR] --month YYYY-MM [--output FILE]\n"
            "       trafficexport [--journal-dir DIR] --start YYYY-MM-DD --end YYYY-MM-DD "
            "[--output FILE]\n");
}

static bool tm_export_parse_month(const char *text, tm_date *start, tm_date *end)
{
    int year;
    unsigned int month;
    char extra;
    if (text == NULL || start == NULL || end == NULL || strlen(text) != 7 ||
        text[4] != '-' || text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9' || text[2] < '0' || text[2] > '9' ||
        text[3] < '0' || text[3] > '9' || text[5] < '0' || text[5] > '9' ||
        text[6] < '0' || text[6] > '9' ||
        sscanf(text, "%d-%u%c", &year, &month, &extra) != 2 || year < 1 || year > 9999 ||
        month == 0 || month > 12) {
        return false;
    }
    *start = (tm_date){year, (uint8_t)month, 1};
    *end = (tm_date){year, (uint8_t)month, 31};
    while (!tm_date_is_valid(*end)) {
        end->day--;
    }
    return true;
}

int main(int argc, char **argv)
{
    tm_export_options options;
    tm_export_stats stats;
    const char *month = NULL;
    const char *start = NULL;
    const char *end = NULL;
    const char *output = NULL;
    char default_output[64];
    memset(&options, 0, sizeof(options));
    options.journal_directory = "/var/lib/trafficmon/journal";
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("trafficexport " TRAFFICMON_VERSION);
        return 0;
    }
    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        const char *value;
        if (index + 1 >= argc) {
            tm_export_usage();
            return 2;
        }
        value = argv[++index];
        if (strcmp(argument, "--journal-dir") == 0) {
            options.journal_directory = value;
        } else if (strcmp(argument, "--month") == 0) {
            month = value;
        } else if (strcmp(argument, "--start") == 0) {
            start = value;
        } else if (strcmp(argument, "--end") == 0) {
            end = value;
        } else if (strcmp(argument, "--output") == 0) {
            output = value;
        } else {
            tm_export_usage();
            return 2;
        }
    }
    if (month != NULL) {
        if (start != NULL || end != NULL || !tm_export_parse_month(month, &options.start_date,
                                                                   &options.end_date)) {
            tm_export_usage();
            return 2;
        }
        if (output == NULL) {
            (void)snprintf(default_output, sizeof(default_output), "trafficmon-%s.csv", month);
            output = default_output;
        }
    } else {
        if (start == NULL || end == NULL || !tm_date_parse(start, &options.start_date) ||
            !tm_date_parse(end, &options.end_date) ||
            tm_date_encode(options.start_date) > tm_date_encode(options.end_date)) {
            tm_export_usage();
            return 2;
        }
        if (output == NULL) {
            (void)snprintf(default_output, sizeof(default_output), "trafficmon-%s-to-%s.csv",
                           start, end);
            output = default_output;
        }
    }
    if (options.journal_directory[0] == '\0' || output[0] == '\0') {
        tm_export_usage();
        return 2;
    }
    options.output_path = output;
    if (!tm_export_csv(&options, &stats)) {
        fprintf(stderr, "trafficexport: export failed\n");
        return 1;
    }
    fprintf(stderr,
            "trafficexport: exported %" PRIu64 " record(s), skipped %" PRIu64
            " corrupt record(s), %u missing day(s), %u invalid file(s)\n",
            stats.exported_records, stats.corrupt_records, stats.missing_files,
            stats.invalid_files);
    return 0;
}
