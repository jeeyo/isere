#ifndef ISERE_TCP_H_
#define ISERE_TCP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#define ISERE_TCP_LOG_TAG "tcp"

#define TCP_POLL_READ_READY (1 << 0)
#define TCP_POLL_WRITE_READY (1 << 1)
#define TCP_POLL_ERROR_READY (1 << 2)

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp);
int isere_tcp_deinit(isere_tcp_t *tcp);

#ifndef ISERE_TCP_MAX_CONNECTIONS
#define ISERE_TCP_MAX_CONNECTIONS 12
#endif /* ISERE_TCP_MAX_CONNECTIONS */

int isere_tcp_socket_new();
void isere_tcp_close(int fd);
int isere_tcp_listen(int fd, uint16_t port);
int isere_tcp_accept(int fd, char *ip_addr);
ssize_t isere_tcp_recv(int fd, char *buf, size_t len);
ssize_t isere_tcp_write(int fd, const char *buf, size_t len);
int isere_tcp_is_initialized();

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
