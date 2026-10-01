#include "ipc_client.h"

#include "ipc_socket.h"

#include <stdlib.h>
#include <string.h>

bool tm_ipc_client_request(const char *socket_path, uint16_t command, const uint8_t *payload,
                           size_t payload_length, tm_ipc_client_response *response)
{
    static uint32_t next_request_id = 1;
    tm_socket connection;
    tm_ipc_header request;
    tm_ipc_header reply;
    uint8_t header_bytes[TM_IPC_HEADER_SIZE];
    tm_ipc_decode_result decode_result;
    bool success = false;

    if (socket_path == NULL || response == NULL || payload_length > TM_IPC_MAX_REQUEST_SIZE ||
        (payload_length != 0 && payload == NULL) || !tm_socket_runtime_init()) {
        return false;
    }
    memset(response, 0, sizeof(*response));
    if (!tm_socket_connect_local(socket_path, &connection)) {
        tm_socket_runtime_cleanup();
        return false;
    }
    request = (tm_ipc_header){TM_IPC_PROTOCOL_VERSION, command, 0, (uint32_t)payload_length,
                              next_request_id++};
    tm_ipc_header_encode(&request, header_bytes);
    if (!tm_socket_send_all(connection, header_bytes, sizeof(header_bytes)) ||
        (payload_length != 0 && !tm_socket_send_all(connection, payload, payload_length)) ||
        tm_socket_receive_all(connection, header_bytes, sizeof(header_bytes)) != 1) {
        goto done;
    }
    decode_result = tm_ipc_header_decode(header_bytes, TM_IPC_MAX_RESPONSE_SIZE, &reply);
    if (decode_result != TM_IPC_DECODE_OK || reply.command != (uint16_t)(command | TM_IPC_RESPONSE_BIT) ||
        reply.request_id != request.request_id || reply.flags > TM_IPC_STATUS_INTERNAL_ERROR) {
        goto done;
    }
    if (reply.payload_length != 0) {
        response->payload = malloc(reply.payload_length);
        if (response->payload == NULL ||
            tm_socket_receive_all(connection, response->payload, reply.payload_length) != 1) {
            free(response->payload);
            response->payload = NULL;
            goto done;
        }
    }
    response->status = (tm_ipc_status)reply.flags;
    response->payload_length = reply.payload_length;
    success = true;

done:
    tm_socket_close(connection);
    tm_socket_runtime_cleanup();
    return success;
}

void tm_ipc_client_response_destroy(tm_ipc_client_response *response)
{
    if (response != NULL) {
        free(response->payload);
        memset(response, 0, sizeof(*response));
    }
}
