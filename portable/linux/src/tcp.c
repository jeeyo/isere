#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <sys/socket.h>
#include <arpa/inet.h>

static isere_t *__isere = NULL;

static tcp_socket_t __sockets[ISERE_TCP_MAX_CONNECTIONS];

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  for (int i = 0; i < ISERE_TCP_MAX_CONNECTIONS; i++) {
    tcp_socket_t *socket = &__sockets[i];
    memset(socket, 0, sizeof(tcp_socket_t));

    socket->fd = -1;
    socket->events = 0;
    socket->revents = 0;
  }

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere = NULL;
  }

  return 0;
}

static tcp_socket_t *__tcp_get_free_slot()
{
  for (int i = 0; i < ISERE_TCP_MAX_CONNECTIONS; i++) {
    if (__sockets[i].fd == -1) {
      return &__sockets[i];
    }
  }

  return NULL;
}

tcp_socket_t *isere_tcp_socket_new()
{
  tcp_socket_t *sock = __tcp_get_free_slot();
  if (sock == NULL) {
    return NULL;
  }

  int ret = isere_tcp_socket_init(sock);
  if (ret < 0) {
    return NULL;
  }

  return sock;
}

int isere_tcp_socket_init(tcp_socket_t *sock)
{
  sock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  sock->events = 0;
  sock->revents = 0;

  if (sock->fd < 0) {
    sock->fd = -1;
    return -1;
  }

  return 0;
}

void isere_tcp_socket_close(tcp_socket_t *sock)
{
  close(sock->fd);

  sock->fd = -1;
  sock->events = 0;
  sock->revents = 0;
}

int isere_tcp_listen(tcp_socket_t *sock, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = bind(sock->fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = listen(sock->fd, ISERE_TCP_MAX_CONNECTIONS);
  if (err != 0) {
    return -1;
  }

  return 0;
}

tcp_socket_t *isere_tcp_accept(tcp_socket_t *sock, char *ip_addr)
{
  tcp_socket_t *newsock = __tcp_get_free_slot();
  if (newsock == NULL) {
    return NULL;
  }

  // reserve this new socket
  newsock->fd = -2;

  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  newsock->fd = accept(sock->fd, (struct sockaddr *)&source_addr, &addr_len);
  if (newsock->fd < 0 && errno == EINTR) {
    newsock->fd = -1;
    return NULL;
  }

  if (newsock->fd < 0) {
    newsock->fd = -1;
    return NULL;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  setsockopt(newsock->fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return newsock;
}

ssize_t isere_tcp_recv(tcp_socket_t *sock, char *buf, size_t len)
{
  int recvd = recv(sock->fd, buf, len, 0);
  if (recvd < 0) {
    if (errno == EINTR) {
      return -2;
    }

    return -1;
  }

  return recvd;
}

ssize_t isere_tcp_write(tcp_socket_t *sock, const char *buf, size_t len)
{
  return write(sock->fd, buf, len);
}

int isere_tcp_poll(tcp_socket_t *sock, int timeout_ms)
{
  if (sock->fd < 0) {
    return -1;
  }

  sock->revents = 0;

  struct pollfd pfd;
  pfd.fd = sock->fd;
  pfd.events = POLLIN;

  int ready = poll(&pfd, 1, timeout_ms);
  if (ready < 0 && errno == EINTR) {
    return -1;
  }

  // TODO: POLLOUT & POLLERR
  if (pfd.revents & POLLIN) {
    sock->revents |= TCP_POLL_READ_READY;
  }

  return sock->revents > 0 ? 1 : 0;
}

int isere_tcp_is_initialized()
{
  return 1;
}
