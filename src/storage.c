#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define TM_FILENO _fileno
#define TM_FSYNC _commit
#else
#include <fcntl.h>
#include <unistd.h>
#define TM_FILENO fileno
#define TM_FSYNC fsync
#endif

static bool tm_temp_path(const char *final_path, char output[TM_PATH_MAX])
{
    int written;
    if (final_path == NULL || final_path[0] == '\0') {
        return false;
    }
    written = snprintf(output, TM_PATH_MAX, "%s.tmp", final_path);
    return written > 0 && written < TM_PATH_MAX;
}

bool tm_storage_ensure_directory(const char *path)
{
    struct stat status;
    if (path == NULL || path[0] == '\0' || strlen(path) >= TM_PATH_MAX) {
        return false;
    }
    if (stat(path, &status) == 0) {
#ifdef _WIN32
        return (status.st_mode & _S_IFMT) == _S_IFDIR;
#else
        return S_ISDIR(status.st_mode);
#endif
    }
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0750) == 0 || errno == EEXIST;
#endif
}

static bool tm_write_all(FILE *stream, const uint8_t *data, size_t length)
{
    size_t written = 0;
    while (written < length) {
        size_t amount = fwrite(data + written, 1, length - written, stream);
        if (amount == 0) {
            return false;
        }
        written += amount;
    }
    return true;
}

bool tm_storage_write_temp(const char *final_path, const uint8_t *data, size_t length)
{
    char temp_path[TM_PATH_MAX];
    FILE *stream;
    bool success;
    if (!tm_temp_path(final_path, temp_path) || data == NULL || length == 0) {
        return false;
    }
    stream = fopen(temp_path, "wb");
    if (stream == NULL) {
        return false;
    }
    success = tm_write_all(stream, data, length) && fflush(stream) == 0 &&
              TM_FSYNC(TM_FILENO(stream)) == 0;
    if (fclose(stream) != 0) {
        success = false;
    }
    if (!success) {
        remove(temp_path);
    }
    return success;
}

#ifndef _WIN32
static void tm_fsync_parent_best_effort(const char *path)
{
    char directory[TM_PATH_MAX];
    char *slash;
    int descriptor;
    size_t length = strlen(path);
    if (length >= sizeof(directory)) {
        return;
    }
    memcpy(directory, path, length + 1);
    slash = strrchr(directory, '/');
    if (slash == NULL) {
        memcpy(directory, ".", 2);
    } else if (slash == directory) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    descriptor = open(directory, O_RDONLY);
    if (descriptor >= 0) {
        (void)fsync(descriptor);
        close(descriptor);
    }
}
#endif

bool tm_storage_replace_temp(const char *final_path)
{
    char temp_path[TM_PATH_MAX];
    if (!tm_temp_path(final_path, temp_path)) {
        return false;
    }
#ifdef _WIN32
    return MoveFileExA(temp_path, final_path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (rename(temp_path, final_path) != 0) {
        return false;
    }
    tm_fsync_parent_best_effort(final_path);
    return true;
#endif
}

bool tm_storage_commit_database(const char *final_path, const tm_aggregator *aggregator,
                                const tm_database_metadata *metadata)
{
    tm_database_buffer buffer;
    tm_database_result result = tm_database_encode(aggregator, metadata, &buffer);
    bool success;
    if (result != TM_DATABASE_OK) {
        return false;
    }
    success = tm_storage_write_temp(final_path, buffer.data, buffer.length) &&
              tm_storage_replace_temp(final_path);
    tm_database_buffer_destroy(&buffer);
    return success;
}

static tm_database_result tm_read_file(const char *path, uint8_t **data, size_t *length)
{
    FILE *stream;
    long file_length;
    size_t maximum = TM_DATABASE_HEADER_SIZE +
                     (size_t)TM_DATABASE_HARD_RECORD_LIMIT * TM_DATABASE_RECORD_SIZE;
    uint8_t *buffer;
    if (path == NULL || data == NULL || length == NULL || strlen(path) >= TM_PATH_MAX) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    *data = NULL;
    *length = 0;
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return TM_DATABASE_INVALID_ARGUMENT;
    }
    if (fseek(stream, 0, SEEK_END) != 0 || (file_length = ftell(stream)) < 0 ||
        (unsigned long)file_length > maximum || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return TM_DATABASE_INVALID_LENGTH;
    }
    if (file_length == 0) {
        fclose(stream);
        return TM_DATABASE_INVALID_LENGTH;
    }
    buffer = malloc((size_t)file_length);
    if (buffer == NULL) {
        fclose(stream);
        return TM_DATABASE_NO_MEMORY;
    }
    {
        size_t read_length = fread(buffer, 1, (size_t)file_length, stream);
        int close_result = fclose(stream);
        if (read_length != (size_t)file_length || close_result != 0) {
            free(buffer);
            return TM_DATABASE_INVALID_LENGTH;
        }
    }
    *data = buffer;
    *length = (size_t)file_length;
    return TM_DATABASE_OK;
}

tm_database_result tm_storage_load_database(const char *path, size_t record_limit,
                                            tm_aggregator *aggregator,
                                            tm_database_metadata *metadata)
{
    uint8_t *data;
    size_t length;
    tm_database_result result = tm_read_file(path, &data, &length);
    if (result != TM_DATABASE_OK) {
        return result;
    }
    result = tm_database_decode(data, length, record_limit, aggregator, metadata);
    free(data);
    return result;
}

tm_database_result tm_storage_validate_file(const char *path, size_t record_limit,
                                            tm_database_metadata *metadata)
{
    uint8_t *data;
    size_t length;
    tm_database_result result = tm_read_file(path, &data, &length);
    if (result != TM_DATABASE_OK) {
        return result;
    }
    result = tm_database_validate(data, length, record_limit, metadata);
    free(data);
    return result;
}

bool tm_storage_file_exists(const char *path)
{
    struct stat status;
    if (path == NULL || stat(path, &status) != 0) {
        return false;
    }
#ifdef _WIN32
    return (status.st_mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(status.st_mode);
#endif
}
