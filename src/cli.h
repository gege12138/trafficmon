#ifndef TRAFFICMON_CLI_H
#define TRAFFICMON_CLI_H

#include <stdbool.h>

enum {
    TM_CLI_SUCCESS = 0,
    TM_CLI_GENERAL_FAILURE = 1,
    TM_CLI_INVALID_USAGE = 2,
    TM_CLI_DAEMON_UNAVAILABLE = 3,
    TM_CLI_PROTOCOL_ERROR = 4,
    TM_CLI_DATABASE_ERROR = 5,
};

int tm_cli_main(int argc, char **argv, bool allow_test_commands);

#endif
