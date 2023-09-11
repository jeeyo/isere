#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static isere_t *__isere = NULL;

int tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  return 0;
}

int tcp_deinit(isere_tcp_t *tcp)
{
  return 0;
}

int tcp_socket_new()
{
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "tcp_socket_new() error: %s", strerror(errno));
  }

  return sock;
}

void tcp_socket_close(int sock)
{
  close(sock);
}

int tcp_listen(int sock, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "tcp_listen() address bind error: %s", strerror(errno));
    return -1;
  }

  err = listen(sock, 1);
  if (err != 0) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "tcp_listen() listen error: %s", strerror(errno));
    return -1;
  }

  return 0;
}

int tcp_accept(int sock, char *ip_addr)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int fd = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
  if (fd < 0 && errno == EINTR) {
    return -2;
  }

  if (fd < 0) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "tcp_accept() error: %s (%d)", strerror(errno));
    return -1;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return fd;
}

int tcp_recv(int sock, char *buf, size_t len)
{
  int recvd = recv(sock, buf, len, 0);
  if (recvd < 0) {
    if (errno == EINTR) {
      return -2;
    }

    __isere->logger->error(ISERE_TCP_LOG_TAG, "tcp_recv() error: %s", strerror(errno));
    return -1;
  }

  return recvd;
}

int tcp_write(int sock, const char *buf, size_t len)
{
  return write(sock, buf, len);
}