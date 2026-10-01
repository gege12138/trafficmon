#ifndef TRAFFICMON_IPC_SOCKET_H
#define TRAFFICMON_IPC_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
typedef intptr_t tm_socket;
#define TM_INVALID_SOCKET ((tm_socket)-1)
#else
typedef int tm_socket;
#define TM_INVALID_SOCKET (-1)
#endif

bool tm_socket_runtime_init(void);
void tm_socket_runtime_cleanup(void);
bool tm_socket_listen_local(const char *path, tm_socket *socket_out);
bool tm_socket_connect_local(const char *path, tm_socket *socket_out);
tm_socket tm_socket_accept(tm_socket listener);
void tm_socket_close(tm_socket socket_value);
bool tm_socket_send_all(tm_socket socket_value, const uint8_t *data, size_t length);
/* Returns 1 on complete read, 0 on clean disconnect before any byte, -1 on error/truncation. */
int tm_socket_receive_all(tm_socket socket_value, uint8_t *data, size_t length);
bool tm_socket_remove_path(const char *path);

#endif
