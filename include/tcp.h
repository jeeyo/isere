#ifndef ISERE_TCP_H_

#include "isere.h"

#include <poll.h>

#define ISERE_TCP_H_

#define ISERE_TCP_LOG_TAG "tcp"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_POLL_READ (1 << 0)
#define TCP_POLL_WRITE (1 << 1)
#define TCP_POLL_ERROR (1 << 2)

#define TCP_POLL_READ_READY (1 << 0)
#define TCP_POLL_WRITE_READY (1 << 1)
#define TCP_POLL_ERROR_READY (1 << 2)

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp);
int isere_tcp_deinit(isere_tcp_t *tcp);

#ifndef ISERE_TCP_MAX_CONNECTIONS
#define ISERE_TCP_MAX_CONNECTIONS 12
#endif

typedef struct {
  int fd;
  short events;
  short revents;
} tcp_socket_t;

tcp_socket_t *isere_tcp_socket_new();
int isere_tcp_socket_init(tcp_socket_t *sock);
void isere_tcp_socket_close(tcp_socket_t *sock);
int isere_tcp_listen(tcp_socket_t *sock, uint16_t port);
tcp_socket_t *isere_tcp_accept(tcp_socket_t *sock, char *ip_addr);
ssize_t isere_tcp_recv(tcp_socket_t *sock, char *buf, size_t len);
ssize_t isere_tcp_write(tcp_socket_t *sock, const char *buf, size_t len);
int isere_tcp_poll(int timeout_ms);
int isere_tcp_is_initialized();

#ifdef __cplusplus
}
#endif

#endif /* ISERE_TCP_H_ */
