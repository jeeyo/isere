#ifndef ISERE_TCP_H_

#include "isere.h"

#define ISERE_TCP_H_

#define ISERE_TCP_LOG_TAG "tcp"

#ifdef __cplusplus
extern "C" {
#endif

#define ISERE_TCP_MAX_CONNECTIONS 12

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp);
int isere_tcp_deinit(isere_tcp_t *tcp);

platform_socket_t isere_tcp_socket_new();
void isere_tcp_socket_close(platform_socket_t sock);
int isere_tcp_listen(platform_socket_t sock, uint16_t port);
platform_socket_t isere_tcp_accept(platform_socket_t sock, char *ip_addr);
ssize_t isere_tcp_recv(platform_socket_t sock, char *buf, size_t len);
ssize_t isere_tcp_write(platform_socket_t sock, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
