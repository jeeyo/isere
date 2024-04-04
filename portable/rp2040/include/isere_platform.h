#ifndef ISERE_PLATFORM_H
#define ISERE_PLATFORM_H

#include "lwip/tcp.h"

typedef struct tcp_pcb __internal_platform_socket_t;
typedef __internal_platform_socket_t *platform_socket_t;
#define PLATFORM_SOCKET_INVALID NULL

void platform_init();

#endif /* ISERE_PLATFORM_H */
