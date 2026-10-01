#include "cli.h"

#ifndef TRAFFICMON_ENABLE_TEST_HOOKS
#define TRAFFICMON_ENABLE_TEST_HOOKS 0
#endif

int main(int argc, char **argv)
{
    return tm_cli_main(argc, argv, TRAFFICMON_ENABLE_TEST_HOOKS != 0);
}
