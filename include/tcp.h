#ifndef ISERE_TCP_H_
#define ISERE_TCP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere_config.h"

#include <stdlib.h>
#include <stdint.h>

#if defined(__linux__)
#include <poll.h>
#endif

#if !defined(__linux__)
#include "lwip/sockets.h"
#endif

#define ISERE_TCP_LOG_TAG "tcp"

typedef void * isere_tcp_t;

int isere_tcp_init(isere_tcp_t *tcp);
int isere_tcp_deinit(isere_tcp_t *tcp);

#ifndef ISERE_TCP_MAX_CONNECTIONS
#define ISERE_TCP_MAX_CONNECTIONS 12
#endif /* ISERE_TCP_MAX_CONNECTIONS */

int isere_tcp_socket_new();
int isere_tcp_socket_set_reuse(int fd);
int isere_tcp_socket_set_nonblock(int fd);
int isere_tcp_socket_last_error(int fd);
void isere_tcp_close(int fd);
int isere_tcp_connect(int fd, const char *ipaddr, uint16_t port);
int isere_tcp_listen(int fd, uint16_t port);
int isere_tcp_accept(int fd, char *ip_addr);
ssize_t isere_tcp_recv(int fd, char *buf, size_t len);
ssize_t isere_tcp_write(int fd, const char *buf, size_t len);
int isere_tcp_poll(struct pollfd *fds, unsigned int nfds, int timeout);
int isere_tcp_is_initialized();

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
