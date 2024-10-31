#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <sys/socket.h>
#include <arpa/inet.h>

static isere_t *__isere = NULL;

static uint32_t __num_of_tcp_conns = 0;

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  // catch SIGPIPE on EPIPE
  signal(SIGPIPE, SIG_IGN);

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere = NULL;
  }

  return 0;
}

int isere_tcp_socket_new()
{
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  return fd;
}

int isere_tcp_socket_set_reuse(int fd)
{
  int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_socket_set_nonblock(int fd)
{
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_socket_last_error(int fd)
{
  int myerrno = 0;
  socklen_t size = sizeof(myerrno);

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &myerrno, &size) != 0) {
    return errno;
  }

  return myerrno;
}

void isere_tcp_close(int fd)
{
  close(fd);
  __num_of_tcp_conns--;
}

int isere_tcp_connect(int fd, const char *ipaddr, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  int err = inet_pton(AF_INET, ipaddr, &dest_addr.sin_addr);
  if (err <= 0) {
    return -1;
  }

  err = connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    if (errno == EAGAIN || errno == EINPROGRESS) {
      return -2;
    }

    return -1;
  }

  return 0;
}

int isere_tcp_listen(int fd, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = bind(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = listen(fd, ISERE_TCP_MAX_CONNECTIONS);
  if (err != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_accept(int fd, char *ip_addr)
{
  if (__num_of_tcp_conns >= ISERE_TCP_MAX_CONNECTIONS) {
    return -1;
  }

  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int newfd = accept(fd, (struct sockaddr *)&source_addr, &addr_len);
  if (newfd < 0 && errno != EINTR) {
    return -1;
  }

  if (newfd < 0) {
    return -1;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  setsockopt(newfd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  __num_of_tcp_conns++;
  return newfd;
}

ssize_t isere_tcp_recv(int fd, char *buf, size_t len)
{
  int recvd = recv(fd, buf, len, 0);
  if (recvd < 0) {
    if (errno == EINTR) {
      return -2;
    }

    return -1;
  }

  return recvd;
}

ssize_t isere_tcp_write(int fd, const char *buf, size_t len)
{
  return write(fd, buf, len);
}

int isere_tcp_poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
  return poll(fds, nfds, timeout);
}

int isere_tcp_is_initialized()
{
  return 1;
}
