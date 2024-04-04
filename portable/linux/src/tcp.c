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
static TaskHandle_t tcp_task_handle;
QueueHandle_t tcp_queue;

static void __tcp_task(void *params);

int32_t isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  tcp_queue = xQueueCreate(10, sizeof(platform_socket_t *));
  if (tcp_queue == NULL) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tcp queue");
    return -1;
  }

  if (xTaskCreate(__tcp_task, "tcp", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &tcp_task_handle) != pdPASS) {
    __isere->logger->error(ISERE_LOG_TAG, "Unable to create tcp task");
    return -1;
  }

  return 0;
}

int32_t isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere = NULL;
  }

  return 0;
}

int32_t isere_tcp_socket_new()
{
  int32_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    return -1;
  }

  return sock;
}

void isere_tcp_socket_close(platform_socket_t sock)
{
  close(sock);
}

int32_t isere_tcp_listen(platform_socket_t sock, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int32_t err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = listen(sock, 1);
  if (err != 0) {
    return -1;
  }

  return 0;
}

platform_socket_t isere_tcp_accept(platform_socket_t sock, char *ip_addr)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  platform_socket_t fd = accept(sock, (struct sockaddr *)&source_addr, &addr_len);
  if (fd < 0 && errno == EINTR) {
    return -2;
  }

  if (fd < 0) {
    return -1;
  }

  // disable tcp keepalive
  int32_t keep_alive = 0;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return fd;
}

int32_t isere_tcp_recv(platform_socket_t sock, char *buf, size_t len)
{
  int32_t recvd = recv(sock, buf, len, 0);
  if (recvd < 0) {
    if (errno == EINTR) {
      return -2;
    }

    return -1;
  }

  return recvd;
}

int32_t isere_tcp_write(int32_t sock, const char *buf, size_t len)
{
  return write(sock, buf, len);
}

static void __tcp_task(void *params)
{
  vTaskDelete(NULL);
}
