#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <sys/socket.h>
#include <arpa/inet.h>

static isere_t *__isere = NULL;
QueueHandle_t tcp_queue;

static void __tcp_task(void *params);

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  tcp_queue = xQueueCreate(ISERE_TCP_MAX_CONNECTIONS, sizeof(platform_socket_t *));
  if (tcp_queue == NULL) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tcp queue");
    return -1;
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

int isere_tcp_socket_new()
{
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    return PLATFORM_SOCKET_INVALID;
  }

  return sock;
}

void isere_tcp_socket_close(platform_socket_t sock)
{
  close(sock);
}

int isere_tcp_listen(platform_socket_t sock, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return PLATFORM_SOCKET_INVALID;
  }

  err = listen(sock, ISERE_TCP_MAX_CONNECTIONS);
  if (err != 0) {
    return PLATFORM_SOCKET_INVALID;
  }

  return 0;
}

platform_socket_t isere_tcp_accept(platform_socket_t sock, char *ip_addr)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  platform_socket_t fd = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
  if (fd < 0 && errno == EINTR) {
    return PLATFORM_SOCKET_INVALID;
  }

  if (fd < 0) {
    return PLATFORM_SOCKET_INVALID;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return fd;
}

ssize_t isere_tcp_recv(platform_socket_t sock, char *buf, size_t len)
{
  int recvd = recv(sock, buf, len, 0);
  if (recvd < 0) {
    if (errno == EINTR) {
      return -2;
    }

    return -1;
  }

  return recvd;
}

ssize_t isere_tcp_write(platform_socket_t sock, const char *buf, size_t len)
{
  return write(sock, buf, len);
}
