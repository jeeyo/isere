#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "tusb_lwip_glue.h"

static uint8_t should_exit = 0;
static uint8_t initialized = 0;

static isere_t *__isere = NULL;

static TaskHandle_t __tusb_task_handle;

static void __isere_tusb_task(void *param);

static void __isere_start_tusb_task()
{
  if (xTaskCreate(__isere_tusb_task, "tusb", 512, NULL, tskIDLE_PRIORITY + 2, &__tusb_task_handle)) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tusb task");
  }
}

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  rndis_tusb_init();

#if configNUMBER_OF_CORES > 1
  multicore_launch_core1(__isere_start_tusb_task);
#else
  __isere_start_tusb_task();
#endif

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere = NULL;
  }

  should_exit = 1;

  return 0;
}

int isere_tcp_socket_new()
{
  int sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
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
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = lwip_bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = lwip_listen(sock, ISERE_TCP_MAX_CONNECTIONS);
  if (err != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_accept(int sock, char *ip_addr)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int fd = lwip_accept(sock, (struct sockaddr *)&source_addr, &addr_len);
  if (fd < 0 && errno == EINTR) {
    return -1;
  }

  if (fd < 0) {
    return -1;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  lwip_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  strncpy(ip_addr, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr), INET_ADDRSTRLEN);

  return fd;
}

int isere_tcp_recv(int sock, char *buf, size_t len)
{
  return lwip_read(sock, buf, len);
}

int isere_tcp_write(int sock, const char *buf, size_t len)
{
  return lwip_write(sock, buf, len);
}

int isere_tcp_is_initialized()
{
  return initialized;
}


// TODO: make this safer
int isere_tcp_poll(int sock[], int num_of_socks, uint8_t revents[], uint8_t events, int timeout_ms)
{
  struct pollfd pfds[num_of_socks];

  for (int i = 0; i < num_of_socks; i++) {
    pfds[i].fd = sock[i];
    pfds[i].revents = 0;
    pfds[i].events = 0;

    if (events & TCP_POLL_READ) {
      pfds[i].events |= POLLIN;
    }
    if (events & TCP_POLL_WRITE) {
      pfds[i].events |= POLLOUT;
    }
    if (events & TCP_POLL_ERROR) {
      pfds[i].events |= POLLERR;
    }
  }

  int ready = lwip_poll(pfds, num_of_socks, timeout_ms);
  if (ready < 0) {
    return -1;
  }

  for (int i = 0; i < num_of_socks; i++) {
    revents[i] = 0;

    /* convert poll flags to our flags */
    if (pfds[i].revents & POLLIN) {
      revents[i] |= TCP_POLL_READ_READY;
    }
    if (pfds[i].revents & POLLOUT) {
      revents[i] |= TCP_POLL_WRITE_READY;
    }
    if (pfds[i].revents & POLLERR) {
      revents[i] |= TCP_POLL_ERROR_READY;
    }
  }

  return 0;
}

static void __isere_tusb_task(void *param)
{
  lwip_freertos_init();
  wait_for_netif_is_up();
  dhcpd_init();
  initialized = 1;

  while (!should_exit)
  {
    tud_task();
    service_traffic();
  }

  should_exit = 1;
  vTaskDelete(NULL);
}
