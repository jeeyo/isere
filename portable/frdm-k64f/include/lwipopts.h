#ifndef LWIPOPTS_H_
#define LWIPOPTS_H_

/* lwIP for isère on the FRDM-K64F: sockets over the tcpip thread, one Ethernet
 * interface (src/ethernetif.c), DHCP. Pools are sized for ISERE_TCP_MAX_CONNECTIONS
 * plus a listening socket and an outbound fetch, in 256 KB of SRAM. */

#define NO_SYS                          0
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    1
#define LWIP_SOCKET_POLL                1
#define LWIP_TIMEVAL_PRIVATE            0
#define LWIP_ERRNO_STDINCLUDE           1
#define LWIP_COMPAT_SOCKETS             0
#define LWIP_POSIX_SOCKETS_IO_NAMES     0

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DHCP                       1
#define LWIP_DNS                        1
#define LWIP_TCP_KEEPALIVE              1
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_SO_RCVTIMEO                1
#define SO_REUSE                        1
#define LWIP_DHCP_DOES_ACD_CHECK        0

/* the verifier fetches its JWKS from 127.0.0.1 (app/routes.json), and fetches to the
 * board's own address must not go out on the wire */
#define LWIP_HAVE_LOOPIF                1
#define LWIP_NETIF_LOOPBACK             1
#define LWIP_LOOPBACK_MAX_PBUFS         8
#define DHCP_DOES_ARP_CHECK             0

/* memory: lwIP's own heap for PBUF_RAM (outgoing data) and pools for the rest */
#define MEM_LIBC_MALLOC                 0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (10 * 1024)
#define PBUF_POOL_SIZE                  8
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                24
#define MEMP_NUM_NETCONN                8
#define MEMP_NUM_NETBUF                 4
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_ARP_QUEUE              8
#define MEMP_NUM_SYS_TIMEOUT            (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 3)

#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
/* closed connections leave TIME_WAIT PCBs; reuse them rather than refusing connections */
#define TCP_LISTEN_BACKLOG              1

/* checksums in software for now; the ENET's checksum accelerators are a later optimisation */
#define LWIP_CHECKSUM_ON_COPY           1

#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

/* threads */
#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          1024
#define TCPIP_THREAD_PRIO               (4)
#define TCPIP_MBOX_SIZE                 16
#define DEFAULT_THREAD_STACKSIZE        1024
#define DEFAULT_THREAD_PRIO             (3)
#define DEFAULT_RAW_RECVMBOX_SIZE       8
#define DEFAULT_UDP_RECVMBOX_SIZE       8
#define DEFAULT_TCP_RECVMBOX_SIZE       8
#define DEFAULT_ACCEPTMBOX_SIZE         8
/* stack sizes above are in words for the FreeRTOS port */
#define LWIP_FREERTOS_THREAD_STACKSIZE_IS_STACKWORDS 1

#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1
#define SYS_LIGHTWEIGHT_PROT            1

/* wall-clock time from SNTP (pool.ntp.org through the DHCP-provided DNS server),
 * for JWT expiry and certificate validity; tcp.c starts it after DHCP */
#include <stdint.h>
void board_set_epoch(uint32_t sec, uint32_t us);
#define SNTP_SERVER_DNS                 1
#define SNTP_STARTUP_DELAY              0
#define SNTP_UPDATE_DELAY               (60 * 60 * 1000)
#define SNTP_SET_SYSTEM_TIME_US(sec, us) board_set_epoch((uint32_t)(sec), (uint32_t)(us))

#endif /* LWIPOPTS_H_ */
