#ifndef TRAFFICMON_DAEMON_H
#define TRAFFICMON_DAEMON_H

#include <stdbool.h>

int tm_daemon_main(int argc, char **argv, bool allow_test_hooks);

#endif
