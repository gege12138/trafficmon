#include "retention/retention.h"
#include "version.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void tm_retention_usage(void)
{
    fprintf(stderr,
            "usage: trafficretention [--journal-dir DIR] [--retention-days DAYS]\n");
}

static bool tm_retention_parse_days(const char *text, uint32_t *days)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || days == NULL || text[0] == '\0' || text[0] == '-') {
        return false;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *days = (uint32_t)value;
    return true;
}

static bool tm_retention_today_utc(tm_date *today)
{
    time_t current = time(NULL);
    struct tm result;

    if (current == (time_t)-1 || today == NULL) {
        return false;
    }
#ifdef _WIN32
    if (gmtime_s(&result, &current) != 0) {
        return false;
    }
#else
    {
        struct tm *utc = gmtime(&current);
        if (utc == NULL) {
            return false;
        }
        result = *utc;
    }
#endif
    *today = (tm_date){result.tm_year + 1900, (uint8_t)(result.tm_mon + 1),
                       (uint8_t)result.tm_mday};
    return tm_date_is_valid(*today);
}

int main(int argc, char **argv)
{
    const char *directory = "/var/lib/trafficmon/journal";
    uint32_t retention_days = 120;
    tm_date today;
    tm_retention_stats stats;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("trafficretention " TRAFFICMON_VERSION);
        return 0;
    }
    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (index + 1 >= argc) {
            tm_retention_usage();
            return 2;
        }
        if (strcmp(argument, "--journal-dir") == 0) {
            directory = argv[++index];
        } else if (strcmp(argument, "--retention-days") == 0) {
            if (!tm_retention_parse_days(argv[++index], &retention_days)) {
                tm_retention_usage();
                return 2;
            }
        } else {
            tm_retention_usage();
            return 2;
        }
    }
    if (directory[0] == '\0' || !tm_retention_today_utc(&today)) {
        fprintf(stderr, "trafficretention: invalid directory or system date\n");
        return 2;
    }
    if (!tm_retention_cleanup(directory, retention_days, today, &stats)) {
        fprintf(stderr,
                "trafficretention: cleanup completed with failures; deleted=%" PRIu64
                " retained=%" PRIu64 " failed=%" PRIu64 " ignored=%" PRIu64 "\n",
                stats.deleted_files, stats.retained_files, stats.delete_failures,
                stats.ignored_entries);
        return 1;
    }
    fprintf(stderr,
            "trafficretention: cleanup complete; deleted=%" PRIu64 " retained=%" PRIu64
            " ignored=%" PRIu64 "\n",
            stats.deleted_files, stats.retained_files, stats.ignored_entries);
    return 0;
}
