#include "server.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/err.h"
#include "lwip/errno.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

static isere_t *__isere = NULL;

int server_init(isere_t *isere)
{
  __isere = isere;
}

int server_deinit(int fd)
{
  if (__isere)
  {
    __isere = NULL;
  }

  // close(fd);
  return 0;
}

void server_task(void *params)
{
  struct sockaddr_storage dest_addr;
  struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
  dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr_ip4->sin_family = AF_INET;
  dest_addr_ip4->sin_port = htons(ISERE_SERVER_PORT);

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_sock < 0)
  {
    __isere->logger->error("Unable to create socket: %s (%d)", strerror(errno), errno);
    vTaskDelete(NULL);
    return;
  }

  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0)
  {
    __isere->logger->error("Socket unable to bind: %s (%d)", strerror(errno), errno);
    goto cleanup;
  }
  __isere->logger->info("Socket bound, port %d", ISERE_SERVER_PORT);

  err = listen(listen_sock, 1);
  if (err != 0)
  {
    __isere->logger->error("Error occurred during listen: %s (%d)", strerror(errno), errno);
    goto cleanup;
  }

  for (;;) {
    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0)
    {
      __isere->logger->error("Unable to accept connection: %s (%d)", strerror(errno), errno);
      break;
    }

    // // set tcp keepalive option
    // setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

    // convert ip address to string
    char addr_str[128];
    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
  }

cleanup:
  close(listen_sock);
  vTaskDelete(NULL);
}
