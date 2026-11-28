#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

int isere_tcp_init(isere_tcp_t *tcp)
{
  // a peer that closes early must not kill the process
  signal(SIGPIPE, SIG_IGN);
  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  return 0;
}

int isere_tcp_socket_new()
{
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return -1;
  }
  if (isere_tcp_socket_set_nonblock(fd) < 0) {
    close(fd);
    return -1;
  }
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
}

int isere_tcp_connect(int fd, const char *ipaddr, uint16_t port)
{
  struct sockaddr_in dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ipaddr, &dest_addr.sin_addr) <= 0) {
    return -1;
  }

  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  if (connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
    if (errno == EAGAIN || errno == EINPROGRESS || errno == EINTR) {
      return ISERE_TCP_AGAIN;
    }
    return -1;
  }

  return 0;
}

int isere_tcp_listen(int fd, uint16_t port)
{
  struct sockaddr_in dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
    return -1;
  }

  if (listen(fd, 64) != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_accept(int fd, char *ip_addr, uint16_t *port)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int newfd = accept(fd, (struct sockaddr *)&source_addr, &addr_len);
  if (newfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED) {
      return ISERE_TCP_AGAIN;
    }
    return -1;
  }

  if (isere_tcp_socket_set_nonblock(newfd) < 0) {
    close(newfd);
    return -1;
  }

  // responses are written in one go; don't let Nagle hold back the last segment
  int yes = 1;
  setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  inet_ntop(AF_INET, &source_addr.sin_addr, ip_addr, INET_ADDRSTRLEN);
  *port = ntohs(source_addr.sin_port);
  return newfd;
}

ssize_t isere_tcp_recv(int fd, char *buf, size_t len)
{
  ssize_t recvd = recv(fd, buf, len, 0);
  if (recvd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return ISERE_TCP_AGAIN;
    }
    return -1;
  }

  return recvd;
}

ssize_t isere_tcp_write(int fd, const char *buf, size_t len)
{
  ssize_t sent = send(fd, buf, len, MSG_NOSIGNAL);
  if (sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return ISERE_TCP_AGAIN;
    }
    return -1;
  }
  return sent;
}

int isere_tcp_local_port(int fd)
{
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
    return -1;
  }
  return ntohs(addr.sin_port);
}

int isere_tcp_resolve(const char *host, char *ip_addr, size_t ip_addr_len)
{
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = NULL;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
    return -1;
  }
  const char *r = inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip_addr, (socklen_t)ip_addr_len);
  freeaddrinfo(res);
  return r != NULL ? 0 : -1;
}

int isere_tcp_poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
  return poll(fds, nfds, timeout);
}

int isere_tcp_is_initialized()
{
  return 1;
}
