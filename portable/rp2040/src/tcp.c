#include "isere.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/inet.h"
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"

#include "tusb_lwip_glue.h"

static isere_t *__isere = NULL;

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  init_lwip();
  wait_for_netif_is_up();
  dhcpd_init();

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
  int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return -1;
  }

  return sock;
}

void isere_tcp_socket_close(int sock)
{
  lwip_close(sock);
}

int isere_tcp_listen(int sock, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);
  dest_addr.sin_port = lwip_htons(port);

  int err = lwip_bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = lwip_listen(sock, 1);
  if (err != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_accept(int sock, char *ip_addr)
{
  struct sockaddr_storage source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int fd = lwip_accept(sock, (struct sockaddr *)&source_addr, &addr_len);
  if (fd < 0 && errno == EINTR) {
    return -2;
  }

  if (fd < 0) {
    return -1;
  }

  // copy ip address string
  strcpy(ip_addr, "0.0.0.0");

  return fd;
}

int isere_tcp_recv(int sock, char *buf, size_t len)
{
  int recvd = lwip_recv(sock, buf, len, 0);
  if (recvd < 0) {
    return -1;
  }

  return recvd;
}

int isere_tcp_write(int sock, const char *buf, size_t len)
{
  return lwip_write(sock, buf, len);
}

void isere_tcp_task(void *params)
{
  for (;;)
  {
    vTaskDelay(50 / portTICK_PERIOD_MS);

    tud_task();
    service_traffic();
  }

  vTaskDelete(NULL);
}
