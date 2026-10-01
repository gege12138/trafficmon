#include "ipc_server.h"

#include "ipc_socket.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

static bool tm_send_response(tm_socket connection, const tm_ipc_header *request,
                             const tm_ipc_server_response *response)
{
    tm_ipc_header reply = {
        .version = TM_IPC_PROTOCOL_VERSION,
        .command = (uint16_t)(request->command | TM_IPC_RESPONSE_BIT),
        .flags = response->status,
        .payload_length = (uint32_t)response->payload_length,
        .request_id = request->request_id,
    };
    uint8_t header[TM_IPC_HEADER_SIZE];
    tm_ipc_header_encode(&reply, header);
    return tm_socket_send_all(connection, header, sizeof(header)) &&
           (response->payload_length == 0 ||
            tm_socket_send_all(connection, response->payload, response->payload_length));
}

static void tm_serve_client(tm_socket connection, tm_ipc_dispatch_fn dispatch, void *context,
                            const volatile sig_atomic_t *stop_requested)
{
    uint8_t header_bytes[TM_IPC_HEADER_SIZE];
    while (!*stop_requested) {
        tm_ipc_header request;
        tm_ipc_server_response response;
        uint8_t *payload = NULL;
        int receive_result = tm_socket_receive_all(connection, header_bytes, sizeof(header_bytes));
        if (receive_result <= 0) {
            return;
        }
        memset(&response, 0, sizeof(response));
        {
            tm_ipc_decode_result decode_result =
                tm_ipc_header_decode(header_bytes, TM_IPC_MAX_REQUEST_SIZE, &request);
            if (decode_result == TM_IPC_DECODE_UNSUPPORTED_VERSION ||
                decode_result == TM_IPC_DECODE_TOO_LARGE) {
                response.status = decode_result == TM_IPC_DECODE_UNSUPPORTED_VERSION
                                      ? TM_IPC_STATUS_UNSUPPORTED_VERSION
                                      : TM_IPC_STATUS_TOO_LARGE;
                (void)tm_send_response(connection, &request, &response);
                return;
            }
            if (decode_result != TM_IPC_DECODE_OK) {
                return;
            }
        }
        if ((request.command & TM_IPC_RESPONSE_BIT) != 0 || request.flags != 0) {
            response.status = TM_IPC_STATUS_BAD_REQUEST;
            (void)tm_send_response(connection, &request, &response);
            return;
        }
        if (request.payload_length != 0) {
            payload = malloc(request.payload_length);
            if (payload == NULL ||
                tm_socket_receive_all(connection, payload, request.payload_length) != 1) {
                free(payload);
                return;
            }
        }
        if (!dispatch(&request, payload, &response, context)) {
            response.status = TM_IPC_STATUS_INTERNAL_ERROR;
            free(response.payload);
            response.payload = NULL;
            response.payload_length = 0;
        }
        free(payload);
        if (response.payload_length > TM_IPC_MAX_RESPONSE_SIZE ||
            (response.payload_length != 0 && response.payload == NULL) ||
            !tm_send_response(connection, &request, &response)) {
            free(response.payload);
            return;
        }
        free(response.payload);
    }
}

bool tm_ipc_server_run_with_tick(const char *socket_path, tm_ipc_dispatch_fn dispatch,
                                 tm_ipc_tick_fn tick, void *context,
                                 const volatile sig_atomic_t *stop_requested)
{
    tm_socket listener;
    bool success = true;
    if (socket_path == NULL || dispatch == NULL || stop_requested == NULL ||
        !tm_socket_runtime_init()) {
        return false;
    }
    (void)tm_socket_remove_path(socket_path);
    if (!tm_socket_listen_local(socket_path, &listener)) {
        tm_socket_runtime_cleanup();
        return false;
    }
    while (!*stop_requested) {
        fd_set readable;
        struct timeval timeout = {0, 100000};
        int selected;
        int select_error = 0;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        selected = select((int)listener + 1, &readable, NULL, NULL, &timeout);
#ifdef _WIN32
        if (selected < 0) {
            select_error = WSAGetLastError();
        }
#else
        if (selected < 0) {
            select_error = errno;
        }
#endif
        if (tick != NULL && !tick(context)) {
            success = false;
            break;
        }
        if (selected == 0) {
            continue;
        }
        if (selected < 0) {
#ifdef _WIN32
            if (select_error == WSAEINTR) {
#else
            if (select_error == EINTR) {
#endif
                continue;
            }
            success = false;
            break;
        }
        tm_socket connection = tm_socket_accept(listener);
        if (connection == TM_INVALID_SOCKET) {
            if (!*stop_requested) {
                success = false;
            }
            break;
        }
        tm_serve_client(connection, dispatch, context, stop_requested);
        tm_socket_close(connection);
    }
    tm_socket_close(listener);
    if (!tm_socket_remove_path(socket_path)) {
        success = false;
    }
    tm_socket_runtime_cleanup();
    return success;
}

bool tm_ipc_server_run(const char *socket_path, tm_ipc_dispatch_fn dispatch, void *context,
                       const volatile sig_atomic_t *stop_requested)
{
    return tm_ipc_server_run_with_tick(socket_path, dispatch, NULL, context, stop_requested);
}
