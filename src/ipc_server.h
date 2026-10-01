#ifndef TRAFFICMON_IPC_SERVER_H
#define TRAFFICMON_IPC_SERVER_H

#include "ipc_protocol.h"

#include <stdbool.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    tm_ipc_status status;
    uint8_t *payload;
    size_t payload_length;
} tm_ipc_server_response;

typedef bool (*tm_ipc_dispatch_fn)(const tm_ipc_header *request, const uint8_t *payload,
                                   tm_ipc_server_response *response, void *context);
typedef bool (*tm_ipc_tick_fn)(void *context);

bool tm_ipc_server_run(const char *socket_path, tm_ipc_dispatch_fn dispatch, void *context,
                       const volatile sig_atomic_t *stop_requested);
bool tm_ipc_server_run_with_tick(const char *socket_path, tm_ipc_dispatch_fn dispatch,
                                 tm_ipc_tick_fn tick, void *context,
                                 const volatile sig_atomic_t *stop_requested);

#endif
