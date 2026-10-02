#include "smb/smb.h"

#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

tm_smb_status tm_smb_upload_csv(const tm_smb_upload_options *options)
{
    (void)options;
    return TM_SMB_CLIENT_UNAVAILABLE;
}

#else

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { TM_SMB_OUTPUT_MAX = 8192, TM_SMB_COMMAND_MAX = 1024, TM_SMB_BUFFER_SIZE = 65536 };

static const char tm_smb_csv_header[] =
    "timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes\r\n";

static bool tm_smb_safe_host(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    for (; *cursor != '\0'; cursor++) {
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= '0' && *cursor <= '9') || *cursor == '.' || *cursor == '-' ||
               *cursor == ':' || *cursor == '[' || *cursor == ']')) {
            return false;
        }
    }
    return true;
}

static bool tm_smb_safe_component(const char *text, bool allow_dollar)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (text == NULL || text[0] == '\0' || strcmp(text, ".") == 0 || strcmp(text, "..") == 0) {
        return false;
    }
    for (; *cursor != '\0'; cursor++) {
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= '0' && *cursor <= '9') || *cursor == '.' || *cursor == '_' ||
               *cursor == '-' || (allow_dollar && *cursor == '$'))) {
            return false;
        }
    }
    return true;
}

static bool tm_smb_safe_remote_directory(const char *text)
{
    const char *cursor;
    const char *component;
    char part[128];
    size_t length;

    if (text == NULL || text[0] == '\0' || strcmp(text, "/") == 0) {
        return true;
    }
    cursor = text[0] == '/' ? text + 1 : text;
    if (*cursor == '\0') {
        return true;
    }
    while (*cursor != '\0') {
        component = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        length = (size_t)(cursor - component);
        if (length == 0 || length >= sizeof(part)) {
            return false;
        }
        memcpy(part, component, length);
        part[length] = '\0';
        if (!tm_smb_safe_component(part, false)) {
            return false;
        }
        if (*cursor == '/') {
            cursor++;
            if (*cursor == '\0') {
                return true;
            }
        }
    }
    return true;
}

static bool tm_smb_safe_credential(const char *text, size_t maximum)
{
    return text != NULL && text[0] != '\0' && strlen(text) < maximum &&
           strchr(text, '\n') == NULL && strchr(text, '\r') == NULL;
}

static bool tm_smb_split_local_path(const char *path, char directory[TM_PATH_MAX],
                                    char name[TM_PATH_MAX])
{
    const char *slash;
    size_t directory_length;
    size_t name_length;

    if (path == NULL || path[0] == '\0' || strlen(path) >= TM_PATH_MAX) {
        return false;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        memcpy(directory, ".", 2);
        name_length = strlen(path);
        memcpy(name, path, name_length + 1);
    } else {
        directory_length = slash == path ? 1U : (size_t)(slash - path);
        name_length = strlen(slash + 1);
        if (directory_length >= TM_PATH_MAX || name_length >= TM_PATH_MAX || name_length == 0) {
            return false;
        }
        memcpy(directory, path, directory_length);
        directory[directory_length] = '\0';
        memcpy(name, slash + 1, name_length + 1);
    }
    name_length = strlen(name);
    return name_length > 4 && strcmp(name + name_length - 4, ".csv") == 0 &&
           tm_smb_safe_component(name, false);
}

static bool tm_smb_validate_csv(const char *path)
{
    FILE *stream = fopen(path, "rb");
    char header[sizeof(tm_smb_csv_header) - 1];
    bool valid = stream != NULL &&
                 fread(header, 1, sizeof(header), stream) == sizeof(header) &&
                 memcmp(header, tm_smb_csv_header, sizeof(header)) == 0 && ferror(stream) == 0;
    if (stream != NULL && fclose(stream) != 0) {
        valid = false;
    }
    return valid;
}

static bool tm_smb_write_auth_file(const tm_smb_upload_options *options,
                                   char path[TM_PATH_MAX])
{
    char temporary[] = "/tmp/trafficmon-smb-auth-XXXXXX";
    int descriptor = mkstemp(temporary);
    FILE *stream;
    bool success;

    if (descriptor < 0) {
        return false;
    }
    stream = fdopen(descriptor, "w");
    if (stream == NULL) {
        close(descriptor);
        unlink(temporary);
        return false;
    }
    success = fprintf(stream, "username = %s\npassword = %s\n", options->username,
                      options->password) > 0;
    if (success) {
        success = fflush(stream) == 0 && fsync(descriptor) == 0;
    }
    if (fclose(stream) != 0) {
        success = false;
    }
    if (!success) {
        unlink(temporary);
        return false;
    }
    memcpy(path, temporary, sizeof(temporary));
    return true;
}

static int tm_smb_run_client(const tm_smb_upload_options *options, const char *auth_file,
                             const char *local_directory, const char *command,
                             char output[TM_SMB_OUTPUT_MAX])
{
    const char *client = options->client_path == NULL ? "smbclient" : options->client_path;
    char service[TM_PATH_MAX];
    FILE *capture = tmpfile();
    int capture_descriptor;
    pid_t child;
    pid_t waited;
    int status;
    size_t count;

    output[0] = '\0';
    if (capture == NULL || snprintf(service, sizeof(service), "//%s/%s", options->host,
                                    options->share) >= (int)sizeof(service)) {
        if (capture != NULL) {
            fclose(capture);
        }
        return -1;
    }
    capture_descriptor = fileno(capture);
    child = fork();
    if (child == 0) {
        if (chdir(local_directory) != 0 || dup2(capture_descriptor, STDOUT_FILENO) < 0 ||
            dup2(capture_descriptor, STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (options->remote_directory != NULL && options->remote_directory[0] != '\0' &&
            strcmp(options->remote_directory, "/") != 0) {
            execlp(client, client, service, "-A", auth_file, "-E", "-d", "1", "-D",
                   options->remote_directory, "-c", command, (char *)NULL);
        } else {
            execlp(client, client, service, "-A", auth_file, "-E", "-d", "1", "-c",
                   command, (char *)NULL);
        }
        _exit(127);
    }
    if (child < 0) {
        fclose(capture);
        return -1;
    }
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    rewind(capture);
    count = fread(output, 1, TM_SMB_OUTPUT_MAX - 1U, capture);
    output[count] = '\0';
    fclose(capture);
    if (waited < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

static tm_smb_status tm_smb_classify_failure(const char *output, tm_smb_status fallback)
{
    if (strstr(output, "NT_STATUS_LOGON_FAILURE") != NULL ||
        strstr(output, "NT_STATUS_ACCESS_DENIED") != NULL) {
        return TM_SMB_AUTH_ERROR;
    }
    if (strstr(output, "NT_STATUS_DISK_FULL") != NULL ||
        strstr(output, "No space left") != NULL) {
        return TM_SMB_REMOTE_SPACE_ERROR;
    }
    if (strstr(output, "NT_STATUS_BAD_NETWORK_NAME") != NULL ||
        strstr(output, "NT_STATUS_OBJECT_PATH_NOT_FOUND") != NULL ||
        strstr(output, "NT_STATUS_OBJECT_NAME_NOT_FOUND") != NULL) {
        return TM_SMB_REMOTE_DIRECTORY_ERROR;
    }
    if (strstr(output, "NT_STATUS_IO_TIMEOUT") != NULL ||
        strstr(output, "NT_STATUS_NETWORK_UNREACHABLE") != NULL ||
        strstr(output, "NT_STATUS_HOST_UNREACHABLE") != NULL ||
        strstr(output, "NT_STATUS_CONNECTION_REFUSED") != NULL ||
        strstr(output, "Connection refused") != NULL || strstr(output, "failed to connect") != NULL) {
        return TM_SMB_NETWORK_ERROR;
    }
    return fallback;
}

static bool tm_smb_files_equal(const char *left_path, const char *right_path)
{
    unsigned char left[TM_SMB_BUFFER_SIZE];
    unsigned char right[TM_SMB_BUFFER_SIZE];
    FILE *left_stream = fopen(left_path, "rb");
    FILE *right_stream = fopen(right_path, "rb");
    bool equal = left_stream != NULL && right_stream != NULL;

    while (equal) {
        size_t left_count = fread(left, 1, sizeof(left), left_stream);
        size_t right_count = fread(right, 1, sizeof(right), right_stream);
        if (left_count != right_count || memcmp(left, right, left_count) != 0) {
            equal = false;
            break;
        }
        if (left_count < sizeof(left)) {
            equal = feof(left_stream) != 0 && feof(right_stream) != 0 &&
                    ferror(left_stream) == 0 && ferror(right_stream) == 0;
            break;
        }
    }
    if (left_stream != NULL) {
        fclose(left_stream);
    }
    if (right_stream != NULL) {
        fclose(right_stream);
    }
    return equal;
}

tm_smb_status tm_smb_upload_csv(const tm_smb_upload_options *options)
{
    char local_directory[TM_PATH_MAX];
    char local_name[TM_PATH_MAX];
    char auth_file[TM_PATH_MAX] = {0};
    char verify_template[] = "/tmp/trafficmon-smb-verify-XXXXXX";
    const char *verify_name;
    char remote_temporary[TM_PATH_MAX];
    char command[TM_SMB_COMMAND_MAX];
    char output[TM_SMB_OUTPUT_MAX];
    int verify_descriptor = -1;
    int result;
    tm_smb_status status = TM_SMB_OK;

    if (options == NULL || !tm_smb_safe_host(options->host) ||
        !tm_smb_safe_component(options->share, true) ||
        !tm_smb_safe_remote_directory(options->remote_directory) ||
        !tm_smb_safe_credential(options->username, 256) ||
        !tm_smb_safe_credential(options->password, 512) ||
        !tm_smb_split_local_path(options->local_csv_path, local_directory, local_name)) {
        return TM_SMB_INVALID_ARGUMENT;
    }
    if (!tm_smb_validate_csv(options->local_csv_path)) {
        return TM_SMB_LOCAL_FILE_ERROR;
    }
    if (!tm_smb_write_auth_file(options, auth_file)) {
        return TM_SMB_LOCAL_FILE_ERROR;
    }
    verify_descriptor = mkstemp(verify_template);
    if (verify_descriptor < 0 || close(verify_descriptor) != 0 || unlink(verify_template) != 0) {
        status = TM_SMB_LOCAL_FILE_ERROR;
        goto cleanup;
    }
    verify_descriptor = -1;
    verify_name = strrchr(verify_template, '/');
    verify_name = verify_name == NULL ? verify_template : verify_name + 1;
    if (snprintf(remote_temporary, sizeof(remote_temporary), ".%s.trafficmon-%ld-%ld.tmp",
                 local_name, (long)getpid(), (long)time(NULL)) >= (int)sizeof(remote_temporary) ||
        snprintf(command, sizeof(command), "put \"%s\" \"%s\"", local_name,
                 remote_temporary) >= (int)sizeof(command)) {
        status = TM_SMB_INVALID_ARGUMENT;
        goto cleanup;
    }
    result = tm_smb_run_client(options, auth_file, local_directory, command, output);
    if (result == 127) {
        status = TM_SMB_CLIENT_UNAVAILABLE;
        goto cleanup_remote;
    }
    if (result != 0) {
        status = tm_smb_classify_failure(output, TM_SMB_UPLOAD_ERROR);
        goto cleanup_remote;
    }
    if (snprintf(command, sizeof(command), "get \"%s\" \"%s\"", remote_temporary,
                 verify_name) >= (int)sizeof(command)) {
        status = TM_SMB_INVALID_ARGUMENT;
        goto cleanup_remote;
    }
    result = tm_smb_run_client(options, auth_file, "/tmp", command, output);
    if (result != 0) {
        status = tm_smb_classify_failure(output, TM_SMB_VERIFY_ERROR);
        goto cleanup_remote;
    }
    if (!tm_smb_files_equal(options->local_csv_path, verify_template)) {
        status = TM_SMB_VERIFY_ERROR;
        goto cleanup_remote;
    }
    if (snprintf(command, sizeof(command), "rename \"%s\" \"%s\"", remote_temporary,
                 local_name) >= (int)sizeof(command)) {
        status = TM_SMB_INVALID_ARGUMENT;
        goto cleanup_remote;
    }
    result = tm_smb_run_client(options, auth_file, "/tmp", command, output);
    if (result != 0) {
        status = tm_smb_classify_failure(output, TM_SMB_RENAME_ERROR);
        goto cleanup_remote;
    }
    goto cleanup;

cleanup_remote:
    if (snprintf(command, sizeof(command), "rm \"%s\"", remote_temporary) <
        (int)sizeof(command)) {
        (void)tm_smb_run_client(options, auth_file, "/tmp", command, output);
    }
cleanup:
    if (verify_descriptor >= 0) {
        close(verify_descriptor);
    }
    unlink(verify_template);
    unlink(auth_file);
    return status;
}

#endif

const char *tm_smb_status_text(tm_smb_status status)
{
    switch (status) {
    case TM_SMB_OK:
        return "success";
    case TM_SMB_INVALID_ARGUMENT:
        return "invalid configuration or path";
    case TM_SMB_LOCAL_FILE_ERROR:
        return "local CSV is missing, unreadable, or invalid";
    case TM_SMB_CLIENT_UNAVAILABLE:
        return "smbclient is unavailable";
    case TM_SMB_NETWORK_ERROR:
        return "network connection failed";
    case TM_SMB_AUTH_ERROR:
        return "authentication failed";
    case TM_SMB_REMOTE_DIRECTORY_ERROR:
        return "remote share or directory does not exist";
    case TM_SMB_REMOTE_SPACE_ERROR:
        return "remote storage has insufficient space";
    case TM_SMB_UPLOAD_ERROR:
        return "temporary upload failed";
    case TM_SMB_VERIFY_ERROR:
        return "uploaded file verification failed";
    case TM_SMB_RENAME_ERROR:
        return "atomic remote rename failed";
    default:
        return "unknown SMB error";
    }
}
