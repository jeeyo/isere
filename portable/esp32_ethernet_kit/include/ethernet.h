#pragma once

#include "esp_err.h"

#define CONFIG_EXAMPLE_ETHERNET_EMAC_TASK_STACK_SIZE 3072
#define EXAMPLE_NETIF_DESC_ETH "example_netif_eth"

#define CONFIG_EXAMPLE_ETH_MDC_GPIO 23
#define CONFIG_EXAMPLE_ETH_MDIO_GPIO 18
#define CONFIG_EXAMPLE_ETH_PHY_RST_GPIO 5
#define CONFIG_EXAMPLE_ETH_PHY_ADDR 1

esp_err_t ethernet_init(void);
void ethernet_deinit(void);
