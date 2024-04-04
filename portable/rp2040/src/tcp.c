#include "isere.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "lwip/tcp.h"

#include "tusb_lwip_glue.h"

static err_t __tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

static isere_t *__isere = NULL;

int32_t isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
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

int32_t isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere = NULL;
  }

  return 0;
}

int32_t isere_tcp_socket_new()
{
  // TODO: 32-bit only
  struct tcp_pcb *sock = tcp_new();
  if (sock == NULL) {
    return -1;
  }

  return (int)sock;
}

void isere_tcp_socket_close(int32_t sock)
{
  tcp_close((struct tcp_pcb *)sock);
}

int32_t isere_tcp_listen(int32_t isock, uint16_t port)
{
  struct tcp_pcb *sock = (struct tcp_pcb *)isock;
  int32_t err = tcp_bind(sock, IP_ADDR_ANY, port);
  if (err != 0) {
    return -1;
  }

  err = tcp_listen(sock);
  if (err != 0) {
    return -1;
  }

  return 0;
}

int32_t isere_tcp_accept(int32_t isock, char *ip_addr)
{
  struct tcp_pcb *sock = (struct tcp_pcb *)isock;
  int32_t err = tcp_accept(sock, __tcp_accept);
  if (err < 0) {
    return -1;
  }

  // // copy ip address string
  // strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return 0;
}

int32_t isere_tcp_recv(int32_t sock, char *buf, size_t len)
{
  // int32_t recvd = recv(sock, buf, len, 0);
  // if (recvd < 0) {
  //   if (errno == EINTR) {
  //     return -2;
  //   }

  //   return -1;
  // }

  // return recvd;
  return -1;
}

int32_t isere_tcp_write(int32_t sock, const char *buf, size_t len)
{
  // return write(sock, buf, len);
  return -1;
}

void isere_tcp_poll()
{
  tud_task();
}

static err_t __tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
}
