#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "lwip/tcp.h"
#include "lwip/inet.h"

#include "tusb_lwip_glue.h"

static err_t __tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t __tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

static isere_t *__isere = NULL;

static QueueHandle_t __tcp_accept_queue;
static QueueHandle_t __tcp_recv_queue;

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  init_lwip();
  wait_for_netif_is_up();
  dhcpd_init();

  __tcp_accept_queue = xQueueCreate(1, sizeof(struct tcp_pcb *));
  if (__tcp_accept_queue == NULL) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tcp accept queue");
    return -1;
  }

  __tcp_recv_queue = xQueueCreate(1, sizeof(struct pbuf *));
  if (__tcp_recv_queue == NULL) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tcp recv queue");
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

platform_socket_t isere_tcp_socket_new()
{
  struct tcp_pcb *sock = tcp_new();
  if (sock == NULL) {
    return PLATFORM_SOCKET_INVALID;
  }

  return (struct tcp_pcb *)sock;
}

void isere_tcp_socket_close(platform_socket_t sock)
{
  tcp_close((struct tcp_pcb *)sock);
}

int isere_tcp_listen(platform_socket_t sock, uint16_t port)
{
  int err = tcp_bind((struct tcp_pcb *)sock, IP_ADDR_ANY, port);
  if (err != 0) {
    return -1;
  }

  struct tcp_pcb *new_sock = tcp_listen((struct tcp_pcb *)sock);
  if (new_sock == NULL) {
    return -1;
  }

  sock = new_sock;

  return 0;
}

platform_socket_t isere_tcp_accept(platform_socket_t sock, char *ip_addr)
{
  platform_socket_t newsock = NULL;
  if (xQueueReceive(__tcp_accept_queue, newsock, portMAX_DELAY) != pdPASS) {
    return PLATFORM_SOCKET_INVALID;
  }

  tcp_recv(sock, __tcp_recv);

  // // copy ip address string
  // strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return newsock;
}

int isere_tcp_recv(platform_socket_t sock, char *buf, size_t len)
{
  struct pbuf *pbuf = NULL;
  if (xQueueReceive(__tcp_recv_queue, pbuf, portMAX_DELAY) != pdPASS) {
    return -1;
  }

  if (pbuf == NULL) {
    return 0;
  }

  return recvd;
}

int isere_tcp_write(platform_socket_t sock, const char *buf, size_t len)
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
  if (xQueueSend(__tcp_accept_queue, newpcb, pdMS_TO_TICKS(50)) != pdPASS) {
    return ERR_MEM;
  }

  return ERR_OK;
}

static err_t __tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
  err_t ret = err;

  if (p == NULL) {
    return ERR_OK;
  }

  if (xQueueSend(__tcp_recv_queue, p, pdMS_TO_TICKS(50)) != pdPASS) {
    return ERR_MEM;
  }

  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);

  return ERR_OK;
}
