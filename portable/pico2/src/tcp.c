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

static TaskHandle_t __tusb_task_handle;

static void __isere_tusb_task(void *param);

int isere_tcp_init(isere_tcp_t *tcp)
{
  if (xTaskCreate(__isere_tusb_task, "usb", 512, NULL, tskIDLE_PRIORITY + 3, &__tusb_task_handle) != pdPASS) {
    return -1;
  }

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  return 0;
}

int isere_tcp_socket_new()
{
  int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (fd >= 0 && isere_tcp_socket_set_nonblock(fd) < 0) {
    lwip_close(fd);
    return -1;
  }
  return fd;
}

int isere_tcp_socket_set_reuse(int fd)
{
  return 0;
}

int isere_tcp_socket_set_nonblock(int fd)
{
  if (lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != 0) {
    return -1;
  }

  return 0;
}

int isere_tcp_socket_last_error(int fd)
{
  int myerrno = 0;
  socklen_t size = sizeof(myerrno);
  if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &myerrno, &size) != 0) {
    return errno;
  }
  return myerrno;
}

void isere_tcp_close(int fd)
{
  lwip_close(fd);
}

int isere_tcp_connect(int fd, const char *ipaddr, uint16_t port)
{
  struct sockaddr_in dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin_len = sizeof(dest_addr);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = PP_HTONS(port);
  dest_addr.sin_addr.s_addr = inet_addr(ipaddr);

  int err = lwip_connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    if (errno == EAGAIN || errno == EINPROGRESS) {
      return ISERE_TCP_AGAIN;
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


int isere_tcp_accept(int fd, char *ip_addr, uint16_t *port)
{
  struct sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);

  int newfd = lwip_accept(fd, (struct sockaddr *)&source_addr, &addr_len);
  if (newfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return ISERE_TCP_AGAIN;
    }
    return -1;
  }

  if (isere_tcp_socket_set_nonblock(newfd) < 0) {
    lwip_close(newfd);
    return -1;
  }

  // disable tcp keepalive
  int keep_alive = 0;
  lwip_setsockopt(newfd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

  // copy ip address string
  inet_ntoa_r(source_addr.sin_addr, ip_addr, INET_ADDRSTRLEN);
  *port = lwip_ntohs(source_addr.sin_port);

  return newfd;
}

ssize_t isere_tcp_recv(int fd, char *buf, size_t len)
{
  int recvd = lwip_recv(fd, buf, len, 0);
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
  ssize_t sent = lwip_write(fd, buf, len);
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return ISERE_TCP_AGAIN;
  }
  return sent;
}

int isere_tcp_local_port(int fd)
{
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (lwip_getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
    return -1;
  }
  return lwip_ntohs(addr.sin_port);
}

int isere_tcp_resolve(const char *host, char *ip_addr, size_t ip_addr_len)
{
  struct hostent *he = lwip_gethostbyname(host);
  if (he == NULL || he->h_addrtype != AF_INET) {
    return -1;
  }
  return inet_ntoa_r(*(struct in_addr *)he->h_addr_list[0], ip_addr, (int)ip_addr_len) != NULL ? 0 : -1;
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

  // TODO: should_exit
  while (1)
  {
    tud_task();
  }

  vTaskDelete(NULL);
}
