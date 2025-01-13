#include "tcp.h"
#include "ethernet.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "ethernet.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

static uint8_t initialized = 0;

static uint32_t __num_of_tcp_conns = 0;

int isere_tcp_init(isere_tcp_t *tcp)
{
  ethernet_init();

  esp_netif_init();
  esp_event_loop_create_default();
  // dhcpd_init();
  initialized = 1;

  return 0;
}

int isere_tcp_deinit(isere_tcp_t *tcp)
{
  esp_event_loop_delete_default();
  esp_netif_deinit();
  ethernet_deinit();
  return 0;
}

int isere_tcp_socket_new()
{
  int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
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

void isere_tcp_close(int fd)
{
  close(fd);
  __num_of_tcp_conns--;
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
      return -2;
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
  lwip_setsockopt(newfd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

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
  return 1;
}
