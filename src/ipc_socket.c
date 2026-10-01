#include "ipc_socket.h"

#include "storage.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <afunix.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

bool tm_socket_runtime_init(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void tm_socket_runtime_cleanup(void)
{
#ifdef _WIN32
    (void)WSACleanup();
#endif
}

static bool tm_socket_address(const char *path, struct sockaddr_un *address, int *length)
{
    size_t path_length;
    if (path == NULL || address == NULL || length == NULL) {
        return false;
    }
    path_length = strlen(path);
    if (path_length == 0 || path_length >= sizeof(address->sun_path)) {
        return false;
    }
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, path_length + 1);
    *length = (int)(offsetof(struct sockaddr_un, sun_path) + path_length + 1);
    return true;
}

bool tm_socket_listen_local(const char *path, tm_socket *socket_out)
{
    struct sockaddr_un address;
    int address_length;
    tm_socket listener;
    if (socket_out == NULL || !tm_socket_address(path, &address, &address_length)) {
        return false;
    }
    *socket_out = TM_INVALID_SOCKET;
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener == TM_INVALID_SOCKET) {
        return false;
    }
    if (bind(listener, (const struct sockaddr *)&address, address_length) != 0 ||
        listen(listener, 16) != 0) {
        tm_socket_close(listener);
        return false;
    }
#ifndef _WIN32
    if (chmod(path, 0660) != 0) {
        tm_socket_close(listener);
        (void)unlink(path);
        return false;
    }
#endif
    *socket_out = listener;
    return true;
}

bool tm_socket_connect_local(const char *path, tm_socket *socket_out)
{
    struct sockaddr_un address;
    int address_length;
    tm_socket connection;
    if (socket_out == NULL || !tm_socket_address(path, &address, &address_length)) {
        return false;
    }
    *socket_out = TM_INVALID_SOCKET;
    connection = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connection == TM_INVALID_SOCKET) {
        return false;
    }
    if (connect(connection, (const struct sockaddr *)&address, address_length) != 0) {
        tm_socket_close(connection);
        return false;
    }
    *socket_out = connection;
    return true;
}

tm_socket tm_socket_accept(tm_socket listener)
{
    tm_socket connection = accept(listener, NULL, NULL);
    if (connection != TM_INVALID_SOCKET) {
#ifdef _WIN32
        DWORD timeout = 1000;
        (void)setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                         sizeof(timeout));
        (void)setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
                         sizeof(timeout));
#else
        struct timeval timeout = {1, 0};
        (void)setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    }
    return connection;
}

void tm_socket_close(tm_socket socket_value)
{
    if (socket_value == TM_INVALID_SOCKET) {
        return;
    }
#ifdef _WIN32
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
}

static bool tm_socket_interrupted(void)
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

bool tm_socket_send_all(tm_socket socket_value, const uint8_t *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        int amount = send(socket_value, (const char *)data + sent, (int)(length - sent), 0);
        if (amount < 0 && tm_socket_interrupted()) {
            continue;
        }
        if (amount <= 0) {
            return false;
        }
        sent += (size_t)amount;
    }
    return true;
}

int tm_socket_receive_all(tm_socket socket_value, uint8_t *data, size_t length)
{
    size_t received = 0;
    while (received < length) {
        int amount = recv(socket_value, (char *)data + received, (int)(length - received), 0);
        if (amount < 0 && tm_socket_interrupted()) {
            continue;
        }
        if (amount == 0) {
            return received == 0 ? 0 : -1;
        }
        if (amount < 0) {
            return -1;
        }
        received += (size_t)amount;
    }
    return 1;
}

bool tm_socket_remove_path(const char *path)
{
    if (path == NULL) {
        return false;
    }
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && DeleteFileA(path) != 0;
#else
    struct stat status;
    if (lstat(path, &status) != 0) {
        return errno == ENOENT;
    }
    return S_ISSOCK(status.st_mode) && unlink(path) == 0;
#endif
}
