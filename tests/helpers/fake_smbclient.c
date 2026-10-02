#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PATH_CAPACITY = 1024, BUFFER_SIZE = 65536 };

static int copy_file(const char *source, const char *destination, int corrupt)
{
    unsigned char buffer[BUFFER_SIZE];
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    int success = input != NULL && output != NULL;
    while (success) {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count != 0 && fwrite(buffer, 1, count, output) != count) {
            success = 0;
        }
        if (count < sizeof(buffer)) {
            success = success && feof(input) != 0 && ferror(input) == 0;
            break;
        }
    }
    if (output != NULL && fclose(output) != 0) {
        success = 0;
    }
    if (input != NULL) {
        fclose(input);
    }
    if (success && corrupt) {
        output = fopen(destination, "r+b");
        if (output == NULL) {
            success = 0;
        } else {
            if (fseek(output, -1, SEEK_END) != 0 || fputc('X', output) == EOF) {
                success = 0;
            }
            if (fclose(output) != 0) {
                success = 0;
            }
        }
    }
    return success ? 0 : 1;
}

static int parse_two_names(const char *command, const char *verb, char first[PATH_CAPACITY],
                           char second[PATH_CAPACITY])
{
    char format[64];
    (void)snprintf(format, sizeof(format), "%s \"%%%zu[^\"]\" \"%%%zu[^\"]\"",
                   verb, (size_t)PATH_CAPACITY - 1U, (size_t)PATH_CAPACITY - 1U);
    return sscanf(command, format, first, second) == 2;
}

static int auth_is_wrong(const char *path)
{
    char line[1024];
    FILE *stream = fopen(path, "r");
    if (stream == NULL) {
        return 1;
    }
    while (fgets(line, sizeof(line), stream) != NULL) {
        if (strstr(line, "password = wrong") != NULL) {
            fclose(stream);
            return 1;
        }
    }
    fclose(stream);
    return 0;
}

static int join_path(char output[PATH_CAPACITY], const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    if (directory_length + 1U + name_length + 1U > PATH_CAPACITY) {
        return 0;
    }
    memcpy(output, directory, directory_length);
    output[directory_length] = '/';
    memcpy(output + directory_length + 1U, name, name_length + 1U);
    return 1;
}

int main(int argc, char **argv)
{
    const char *root = getenv("TM_SMB_FAKE_ROOT");
    const char *mode = getenv("TM_SMB_FAKE_MODE");
    const char *directory = "";
    const char *command = NULL;
    const char *auth = NULL;
    char remote_root[PATH_CAPACITY];
    char first[PATH_CAPACITY];
    char second[PATH_CAPACITY];
    char source[PATH_CAPACITY];
    char destination[PATH_CAPACITY];
    struct stat metadata;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-A") == 0 && index + 1 < argc) {
            auth = argv[++index];
        } else if (strcmp(argv[index], "-D") == 0 && index + 1 < argc) {
            directory = argv[++index];
        } else if (strcmp(argv[index], "-c") == 0 && index + 1 < argc) {
            command = argv[++index];
        }
    }
    if (mode != NULL && strcmp(mode, "network") == 0) {
        fputs("Connection refused\n", stderr);
        return 1;
    }
    if (auth == NULL || auth_is_wrong(auth)) {
        fputs("NT_STATUS_LOGON_FAILURE\n", stderr);
        return 1;
    }
    if (root == NULL || command == NULL ||
        snprintf(remote_root, sizeof(remote_root), "%s/%s", root, directory) >=
            (int)sizeof(remote_root) ||
        stat(remote_root, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        fputs("NT_STATUS_OBJECT_PATH_NOT_FOUND\n", stderr);
        return 1;
    }
    if (strncmp(command, "put ", 4) == 0 && parse_two_names(command, "put", first, second)) {
        if (mode != NULL && strcmp(mode, "space") == 0) {
            fputs("NT_STATUS_DISK_FULL\n", stderr);
            return 1;
        }
        if (!join_path(destination, remote_root, second)) {
            return 1;
        }
        return copy_file(first, destination, mode != NULL && strcmp(mode, "corrupt") == 0);
    }
    if (strncmp(command, "get ", 4) == 0 && parse_two_names(command, "get", first, second)) {
        if (!join_path(source, remote_root, first)) {
            return 1;
        }
        return copy_file(source, second, 0);
    }
    if (strncmp(command, "rename ", 7) == 0 &&
        parse_two_names(command, "rename", first, second)) {
        if (!join_path(source, remote_root, first) ||
            !join_path(destination, remote_root, second)) {
            return 1;
        }
        (void)remove(destination);
        return rename(source, destination) == 0 ? 0 : 1;
    }
    if (sscanf(command, "rm \"%1023[^\"]\"", first) == 1) {
        if (!join_path(source, remote_root, first)) {
            return 1;
        }
        return remove(source) == 0 || errno == ENOENT ? 0 : 1;
    }
    return 1;
}
