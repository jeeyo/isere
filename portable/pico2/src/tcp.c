#include "tcp.h"

#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "tusb_lwip_glue.h"

static uint8_t initialized = 0;

static isere_t *__isere = NULL;

static TaskHandle_t __tusb_task_handle;

static uint32_t __num_of_tcp_conns = 0;

static void __isere_tusb_task(void *param);

int isere_tcp_init(isere_t *isere, isere_tcp_t *tcp)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  if (xTaskCreate(__isere_tusb_task, "usb", 512, NULL, tskIDLE_PRIORITY + 3, &__tusb_task_handle)) {
    __isere->logger->error(ISERE_TCP_LOG_TAG, "Unable to create tusb task");
  }

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  if (__isere) {
    __isere->should_exit = 1;
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
  if (lwip_fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0) {
    return -1;
  }

  return 0;
}

void isere_tcp_close(int fd)
{
  close(fd);
  __num_of_tcp_conns--;
}

int isere_tcp_listen(int fd, uint16_t port)
{
  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(port);

  int err = lwip_bind(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    return -1;
  }

  err = lwip_listen(fd, ISERE_TCP_MAX_CONNECTIONS);
  if (err != 0) {
    return -1;
  }

  return 0;
}


int isere_tcp_accept(int fd, char *ip_addr)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int newfd = lwip_accept(fd, (struct sockaddr *)&source_addr, &addr_len);
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

  return newfd;
}

ssize_t isere_tcp_recv(int fd, char *buf, size_t len)
{
  int recvd = lwip_recv(fd, buf, len, 0);
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
  return lwip_write(fd, buf, len);
}

int isere_tcp_poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
  return lwip_poll(fds, nfds, timeout);
}

int isere_tcp_is_initialized()
{
  return initialized;
}

static void __isere_tusb_task(void *param)
{
  rndis_tusb_init();

  lwip_freertos_init();
  wait_for_netif_is_up();
  dhcpd_init();
  initialized = 1;

  while (!__isere->should_exit)
  {
    tud_task();
  }

  __isere->logger->error(ISERE_TCP_LOG_TAG, "tusb task was unexpectedly closed");
  __isere->should_exit = 1;
  vTaskDelete(NULL);
}
