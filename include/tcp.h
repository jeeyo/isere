#ifndef ISERE_TCP_H_

#include "isere.h"

#define ISERE_TCP_H_

#define ISERE_TCP_LOG_TAG "tcp"

#ifdef __cplusplus
extern "C" {
#endif

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp);
int isere_tcp_deinit(isere_tcp_t *tcp);

#define ISERE_TCP_MAX_CONNECTIONS 5

int isere_tcp_socket_new();
void isere_tcp_socket_close(int sock);
int isere_tcp_listen(int sock, uint16_t port);
int isere_tcp_accept(int sock, char *ip_addr);
ssize_t isere_tcp_recv(int sock, char *buf, size_t len);
ssize_t isere_tcp_write(int sock, const char *buf, size_t len);
int isere_tcp_is_initialized();

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
