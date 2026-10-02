#include "smb/smb.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

static void tm_upload_usage(void)
{
    fprintf(stderr,
            "usage: trafficupload --host HOST --share SHARE [--remote-dir DIR] "
            "--file CSV --credentials-stdin\n");
}

static bool tm_upload_read_line(char *output, size_t capacity)
{
    size_t length;
    if (fgets(output, (int)capacity, stdin) == NULL) {
        return false;
    }
    length = strlen(output);
    if (length == 0 || output[length - 1] != '\n') {
        return false;
    }
    output[--length] = '\0';
    if (length > 0 && output[length - 1] == '\r') {
        output[--length] = '\0';
    }
    return length > 0;
}

static void tm_upload_clear(char *text, size_t length)
{
    volatile char *cursor = text;
    while (length-- > 0) {
        *cursor++ = '\0';
    }
}

int main(int argc, char **argv)
{
    tm_smb_upload_options options;
    tm_smb_status status;
    char username[256];
    char password[512];
    bool credentials_stdin = false;

    memset(&options, 0, sizeof(options));
    options.remote_directory = "";
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("trafficupload " TRAFFICMON_VERSION);
        return 0;
    }
    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--credentials-stdin") == 0) {
            credentials_stdin = true;
            continue;
        }
        if (index + 1 >= argc) {
            tm_upload_usage();
            return 2;
        }
        if (strcmp(argument, "--host") == 0) {
            options.host = argv[++index];
        } else if (strcmp(argument, "--share") == 0) {
            options.share = argv[++index];
        } else if (strcmp(argument, "--remote-dir") == 0) {
            options.remote_directory = argv[++index];
        } else if (strcmp(argument, "--file") == 0) {
            options.local_csv_path = argv[++index];
        } else {
            tm_upload_usage();
            return 2;
        }
    }
    if (!credentials_stdin || !tm_upload_read_line(username, sizeof(username)) ||
        !tm_upload_read_line(password, sizeof(password))) {
        tm_upload_clear(password, sizeof(password));
        tm_upload_usage();
        return 2;
    }
    options.username = username;
    options.password = password;
    status = tm_smb_upload_csv(&options);
    tm_upload_clear(password, sizeof(password));
    if (status != TM_SMB_OK) {
        fprintf(stderr, "trafficupload: %s (host=%s share=%s remote_dir=%s)\n",
                tm_smb_status_text(status), options.host == NULL ? "<invalid>" : options.host,
                options.share == NULL ? "<invalid>" : options.share,
                options.remote_directory == NULL ? "" : options.remote_directory);
        return 1;
    }
    fprintf(stderr, "trafficupload: uploaded %s to //%s/%s/%s\n", options.local_csv_path,
            options.host, options.share, options.remote_directory);
    return 0;
}
