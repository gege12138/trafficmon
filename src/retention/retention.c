#include "retention/retention.h"

#include "storage.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static bool tm_retention_parse_name(const char *name, tm_date *date)
{
    uint32_t encoded = 0;
    size_t index;

    if (name == NULL || date == NULL || strlen(name) != 20 ||
        strncmp(name, "journal-", 8) != 0 || strcmp(name + 16, ".bin") != 0) {
        return false;
    }
    for (index = 8; index < 16; index++) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
        encoded = encoded * 10U + (uint32_t)(name[index] - '0');
    }
    return tm_date_decode(encoded, date);
}

static int64_t tm_retention_days_from_civil(tm_date date)
{
    int64_t year = date.year;
    unsigned int month = date.month;
    unsigned int day = date.day;
    int64_t era;
    unsigned int year_of_era;
    unsigned int day_of_year;
    unsigned int day_of_era;

    year -= month <= 2;
    era = year >= 0 ? year / 400 : (year - 399) / 400;
    year_of_era = (unsigned int)(year - era * 400);
    day_of_year = (153U * (month > 2 ? month - 3 : month + 9) + 2U) / 5U + day - 1U;
    day_of_era = year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return era * 146097 + (int64_t)day_of_era;
}

static bool tm_retention_join_path(const char *directory, const char *name,
                                   char output[TM_PATH_MAX])
{
    int written = snprintf(output, TM_PATH_MAX, "%s/%s", directory, name);
    return written > 0 && written < TM_PATH_MAX;
}

static bool tm_retention_consider(const char *directory, const char *name, int64_t today_days,
                                  uint32_t retention_days, tm_retention_stats *stats)
{
    tm_date file_date;
    int64_t age;
    char path[TM_PATH_MAX];

    stats->entries_scanned++;
    if (!tm_retention_parse_name(name, &file_date)) {
        stats->ignored_entries++;
        return true;
    }
    stats->journal_files++;
    age = today_days - tm_retention_days_from_civil(file_date);
    if (age <= (int64_t)retention_days) {
        stats->retained_files++;
        return true;
    }
    if (!tm_retention_join_path(directory, name, path)) {
        fprintf(stderr, "trafficretention: path is too long for journal shard %s\n", name);
        stats->delete_failures++;
        return false;
    }
    if (remove(path) != 0) {
        int remove_error = errno;
        fprintf(stderr, "trafficretention: unable to delete %s: %s\n", path,
                strerror(remove_error));
        stats->delete_failures++;
        return false;
    }
    stats->deleted_files++;
    return true;
}

bool tm_retention_cleanup(const char *directory, uint32_t retention_days, tm_date today,
                          tm_retention_stats *stats)
{
    tm_retention_stats local_stats;
    int64_t today_days;
    bool success = true;

    if (stats == NULL) {
        stats = &local_stats;
    }
    memset(stats, 0, sizeof(*stats));
    if (directory == NULL || directory[0] == '\0' || !tm_date_is_valid(today)) {
        return false;
    }
    today_days = tm_retention_days_from_civil(today);

#ifdef _WIN32
    {
        char pattern[TM_PATH_MAX];
        WIN32_FIND_DATAA item;
        HANDLE handle;
        DWORD find_error;

        if (!tm_retention_join_path(directory, "*", pattern)) {
            return false;
        }
        handle = FindFirstFileA(pattern, &item);
        if (handle == INVALID_HANDLE_VALUE) {
            find_error = GetLastError();
            return find_error == ERROR_FILE_NOT_FOUND || find_error == ERROR_PATH_NOT_FOUND;
        }
        do {
            if (strcmp(item.cFileName, ".") != 0 && strcmp(item.cFileName, "..") != 0 &&
                !tm_retention_consider(directory, item.cFileName, today_days, retention_days,
                                       stats)) {
                success = false;
            }
        } while (FindNextFileA(handle, &item) != 0);
        find_error = GetLastError();
        if (find_error != ERROR_NO_MORE_FILES) {
            fprintf(stderr, "trafficretention: unable to finish scanning %s (error %lu)\n",
                    directory, (unsigned long)find_error);
            success = false;
        }
        FindClose(handle);
    }
#else
    {
        DIR *stream = opendir(directory);
        struct dirent *item;
        int scan_error;

        if (stream == NULL) {
            if (errno == ENOENT) {
                return true;
            }
            fprintf(stderr, "trafficretention: unable to scan %s: %s\n", directory,
                    strerror(errno));
            return false;
        }
        errno = 0;
        while ((item = readdir(stream)) != NULL) {
            if (strcmp(item->d_name, ".") != 0 && strcmp(item->d_name, "..") != 0 &&
                !tm_retention_consider(directory, item->d_name, today_days, retention_days,
                                       stats)) {
                success = false;
            }
            errno = 0;
        }
        scan_error = errno;
        if (closedir(stream) != 0) {
            fprintf(stderr, "trafficretention: unable to close directory %s: %s\n", directory,
                    strerror(errno));
            success = false;
        }
        if (scan_error != 0) {
            fprintf(stderr, "trafficretention: unable to finish scanning %s: %s\n", directory,
                    strerror(scan_error));
            success = false;
        }
    }
#endif
    return success;
}
