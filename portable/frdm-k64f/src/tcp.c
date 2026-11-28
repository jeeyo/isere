#include "tcp.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/apps/sntp.h"

#include "board.h"

#include "ethernetif.h"

static volatile uint8_t initialized = 0;
static struct netif __netif;

static void __net_task(void *param);

int isere_tcp_init(isere_tcp_t *tcp)
{
  (void)tcp;
  if (xTaskCreate(__net_task, "net", 1024, NULL, tskIDLE_PRIORITY + 3, NULL) != pdPASS) {
    return -1;
  }
  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  (void)tcp;
  return 0;
}

int isere_tcp_socket_new()
{
  int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (fd >= 0 && isere_tcp_socket_set_nonblock(fd) < 0) {
    lwip_close(fd);
    return -1;
  }
  // requests and responses are written whole: Nagle would only hold them for an ACK
  int yes = 1;
  lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
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

  // a response is written whole; with Nagle it would wait for the client's delayed ACK
  // of the previous one on a keep-alive connection
  int nodelay = 1;
  lwip_setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

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

static void __tcpip_ready(void *param)
{
  xSemaphoreGive((SemaphoreHandle_t)param);
}

// brings up lwIP and the Ethernet interface, then waits for DHCP; the httpd task starts
// listening once this sets `initialized`. The address goes to the UART for tools/hil.sh.
static void __net_task(void *param)
{
  (void)param;

  SemaphoreHandle_t ready = xSemaphoreCreateBinary();
  tcpip_init(__tcpip_ready, ready);
  xSemaphoreTake(ready, portMAX_DELAY);
  vSemaphoreDelete(ready);

  LOCK_TCPIP_CORE();
  if (netif_add(&__netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, ethernetif_init, tcpip_input) == NULL) {
    UNLOCK_TCPIP_CORE();
    printf("isere: unable to add the Ethernet interface\n");
    vTaskDelete(NULL);
    return;
  }
  netif_set_default(&__netif);
  netif_set_up(&__netif);
  dhcp_start(&__netif);
  UNLOCK_TCPIP_CORE();

  while (ip4_addr_isany_val(*netif_ip4_addr(&__netif))) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  char ip[IP4ADDR_STRLEN_MAX];
  ip4addr_ntoa_r(netif_ip4_addr(&__netif), ip, sizeof(ip));
  printf("isere: ip %s\n", ip);

  LOCK_TCPIP_CORE();
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();
  UNLOCK_TCPIP_CORE();
  // JWT expiry needs the time of day; serve without it after 10 s rather than never
  for (int i = 0; i < 100 && !board_has_epoch(); i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  printf("isere: %s\n", board_has_epoch() ? "clock set by SNTP" : "no SNTP answer, the clock is time since boot");
  initialized = 1;

  vTaskDelete(NULL);
}
