#ifndef ISERE_TCP_H_

#include "isere.h"

#define ISERE_TCP_H_

#define ISERE_TCP_LOG_TAG "tcp"

#ifdef __cplusplus
extern "C" {
#endif

int tcp_init(isere_t *isere, isere_tcp_t *tcp);
int tcp_deinit(isere_tcp_t *tcp);

int tcp_socket_new();
void tcp_socket_close(int sock);
int tcp_listen(int sock, uint16_t port);
int tcp_accept(int sock, char *ip_addr);
int tcp_recv(int sock, char *buf, size_t len);
int tcp_write(int sock, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
