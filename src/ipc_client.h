#ifndef TRAFFICMON_IPC_CLIENT_H
#define TRAFFICMON_IPC_CLIENT_H

#include "ipc_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    tm_ipc_status status;
    uint8_t *payload;
    size_t payload_length;
} tm_ipc_client_response;

bool tm_ipc_client_request(const char *socket_path, uint16_t command, const uint8_t *payload,
                           size_t payload_length, tm_ipc_client_response *response);
void tm_ipc_client_response_destroy(tm_ipc_client_response *response);

#endif
