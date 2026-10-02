#ifndef TRAFFICMON_SMB_H
#define TRAFFICMON_SMB_H

#include <stdbool.h>

typedef enum {
    TM_SMB_OK = 0,
    TM_SMB_INVALID_ARGUMENT,
    TM_SMB_LOCAL_FILE_ERROR,
    TM_SMB_CLIENT_UNAVAILABLE,
    TM_SMB_NETWORK_ERROR,
    TM_SMB_AUTH_ERROR,
    TM_SMB_REMOTE_DIRECTORY_ERROR,
    TM_SMB_REMOTE_SPACE_ERROR,
    TM_SMB_UPLOAD_ERROR,
    TM_SMB_VERIFY_ERROR,
    TM_SMB_RENAME_ERROR,
} tm_smb_status;

typedef struct {
    const char *host;
    const char *share;
    const char *username;
    const char *password;
    const char *remote_directory;
    const char *local_csv_path;
    /* Test/packaging override. NULL selects smbclient from PATH. */
    const char *client_path;
} tm_smb_upload_options;

tm_smb_status tm_smb_upload_csv(const tm_smb_upload_options *options);
const char *tm_smb_status_text(tm_smb_status status);

#endif
