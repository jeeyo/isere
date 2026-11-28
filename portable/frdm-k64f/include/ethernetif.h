#ifndef ISERE_K64F_ETHERNETIF_H_
#define ISERE_K64F_ETHERNETIF_H_

#include "lwip/netif.h"

// lwIP netif init callback for the K64F's ENET with the board's KSZ8081 PHY.
// Starts a receive task, and keeps the netif's link state in step with the PHY.
err_t ethernetif_init(struct netif *netif);

#endif /* ISERE_K64F_ETHERNETIF_H_ */
