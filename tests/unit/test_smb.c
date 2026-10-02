#include "smb/smb.h"
#include "storage.h"
#include "test.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char smb_header[] =
    "timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes\r\n";

static bool smb_write_csv(const char *path, size_t rows)
{
    FILE *stream = fopen(path, "wb");
    bool success = stream != NULL && fwrite(smb_header, 1, sizeof(smb_header) - 1U, stream) ==
                                              sizeof(smb_header) - 1U;
    for (size_t index = 0; success && index < rows; index++) {
        success = fprintf(stream,
                          "2026-09-01T00:00:00Z,device-%zu,192.0.2.1,198.51.100.1,1234,443,"
                          "6,100,50\r\n",
                          index) > 0;
    }
    if (stream != NULL && fclose(stream) != 0) {
        success = false;
    }
    return success;
}

static bool smb_file_exists(const char *path)
{
    struct stat metadata;
    return stat(path, &metadata) == 0 && S_ISREG(metadata.st_mode);
}

static bool smb_has_temporary(const char *directory)
{
    DIR *stream = opendir(directory);
    struct dirent *item;
    bool found = false;
    if (stream == NULL) {
        return false;
    }
    while ((item = readdir(stream)) != NULL) {
        if (strstr(item->d_name, ".trafficmon-") != NULL) {
            found = true;
            break;
        }
    }
    closedir(stream);
    return found;
}

void test_smb(void)
{
    const char *base = TM_TEST_BINARY_DIR "/smb-test";
    const char *remote = TM_TEST_BINARY_DIR "/smb-test/remote";
    const char *archive = TM_TEST_BINARY_DIR "/smb-test/remote/archive";
    const char *small = TM_TEST_BINARY_DIR "/smb-test/trafficmon-small.csv";
    const char *large = TM_TEST_BINARY_DIR "/smb-test/trafficmon-large.csv";
    const char *corrupt = TM_TEST_BINARY_DIR "/smb-test/trafficmon-corrupt.csv";
    const char *remote_small = TM_TEST_BINARY_DIR "/smb-test/remote/archive/trafficmon-small.csv";
    const char *remote_large = TM_TEST_BINARY_DIR "/smb-test/remote/archive/trafficmon-large.csv";
    tm_smb_upload_options options = {
        "nas.test", "archive", "test-user", "correct", "archive", small,
        TM_FAKE_SMBCLIENT_PATH,
    };
    FILE *stream;

    TM_EXPECT_TRUE(tm_storage_ensure_directory(base));
    TM_EXPECT_TRUE(tm_storage_ensure_directory(remote));
    TM_EXPECT_TRUE(tm_storage_ensure_directory(archive));
    (void)remove(remote_small);
    (void)remove(remote_large);
    TM_EXPECT_TRUE(setenv("TM_SMB_FAKE_ROOT", remote, 1) == 0);
    TM_EXPECT_TRUE(unsetenv("TM_SMB_FAKE_MODE") == 0);

    TM_EXPECT_TRUE(smb_write_csv(small, 1));
    TM_EXPECT_EQ_U64(TM_SMB_OK, tm_smb_upload_csv(&options));
    TM_EXPECT_TRUE(smb_file_exists(remote_small));
    TM_EXPECT_TRUE(!smb_has_temporary(archive));

    options.password = "wrong";
    TM_EXPECT_EQ_U64(TM_SMB_AUTH_ERROR, tm_smb_upload_csv(&options));
    options.password = "correct";

    TM_EXPECT_TRUE(setenv("TM_SMB_FAKE_MODE", "network", 1) == 0);
    TM_EXPECT_EQ_U64(TM_SMB_NETWORK_ERROR, tm_smb_upload_csv(&options));
    TM_EXPECT_TRUE(setenv("TM_SMB_FAKE_MODE", "space", 1) == 0);
    TM_EXPECT_EQ_U64(TM_SMB_REMOTE_SPACE_ERROR, tm_smb_upload_csv(&options));

    options.remote_directory = "missing";
    TM_EXPECT_TRUE(unsetenv("TM_SMB_FAKE_MODE") == 0);
    TM_EXPECT_EQ_U64(TM_SMB_REMOTE_DIRECTORY_ERROR, tm_smb_upload_csv(&options));
    options.remote_directory = "archive";

    stream = fopen(corrupt, "wb");
    TM_EXPECT_TRUE(stream != NULL);
    if (stream != NULL) {
        TM_EXPECT_TRUE(fputs("damaged csv\n", stream) >= 0);
        TM_EXPECT_TRUE(fclose(stream) == 0);
    }
    options.local_csv_path = corrupt;
    TM_EXPECT_EQ_U64(TM_SMB_LOCAL_FILE_ERROR, tm_smb_upload_csv(&options));

    options.local_csv_path = small;
    TM_EXPECT_TRUE(setenv("TM_SMB_FAKE_MODE", "corrupt", 1) == 0);
    TM_EXPECT_EQ_U64(TM_SMB_VERIFY_ERROR, tm_smb_upload_csv(&options));
    TM_EXPECT_TRUE(!smb_has_temporary(archive));

    TM_EXPECT_TRUE(unsetenv("TM_SMB_FAKE_MODE") == 0);
    TM_EXPECT_TRUE(smb_write_csv(large, 200000));
    options.local_csv_path = large;
    TM_EXPECT_EQ_U64(TM_SMB_OK, tm_smb_upload_csv(&options));
    TM_EXPECT_TRUE(smb_file_exists(remote_large));
    TM_EXPECT_TRUE(!smb_has_temporary(archive));

    (void)remove(small);
    (void)remove(large);
    (void)remove(corrupt);
    (void)remove(remote_small);
    (void)remove(remote_large);
    (void)rmdir(archive);
    (void)rmdir(remote);
    (void)rmdir(base);
    (void)unsetenv("TM_SMB_FAKE_ROOT");
}
