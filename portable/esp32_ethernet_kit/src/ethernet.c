#include "ethernet.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

static const char *TAG = "ethernet";

static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created within common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
  return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

static bool netif_desc_matches_with(esp_netif_t *netif, void *ctx)
{
  return strcmp(ctx, esp_netif_get_desc(netif)) == 0;
}

esp_netif_t *get_netif_from_desc(const char *desc)
{
  return esp_netif_find_if(netif_desc_matches_with, (void*)desc);
}

esp_eth_handle_t get_eth_handle(void)
{
  return s_eth_handle;
}

/** Event handler for Ethernet events */
static void eth_on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  if (!is_our_netif(EXAMPLE_NETIF_DESC_ETH, event->esp_netif)) {
    return;
  }
  ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
  xSemaphoreGive(s_semph_get_ip_addrs);
}

static esp_netif_t *eth_start(void)
{
  esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();

  // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
  esp_netif_config.if_desc = EXAMPLE_NETIF_DESC_ETH;
  esp_netif_config.route_prio = 64;
  esp_netif_config_t netif_config = {
      .base = &esp_netif_config,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
  };
  esp_netif_t *netif = esp_netif_new(&netif_config);
  assert(netif);

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  mac_config.rx_task_stack_size = CONFIG_EXAMPLE_ETHERNET_EMAC_TASK_STACK_SIZE;
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
  phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;

  // internal ethernet MAC
  eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  esp32_emac_config.smi_gpio.mdc_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
  esp32_emac_config.smi_gpio.mdio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
  s_mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

  // Install Ethernet driver
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
  ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));

  // combine driver with netif
  s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
  esp_netif_attach(netif, s_eth_glue);

  // Register user defined event handers
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip, NULL));

  esp_eth_start(s_eth_handle);
  return netif;
}

static void eth_stop(void)
{
  esp_netif_t *eth_netif = get_netif_from_desc(EXAMPLE_NETIF_DESC_ETH);
  ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_on_got_ip));
  ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
  ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
  ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
  s_eth_handle = NULL;
  ESP_ERROR_CHECK(s_phy->del(s_phy));
  ESP_ERROR_CHECK(s_mac->del(s_mac));

  esp_netif_destroy(eth_netif);
}

esp_err_t ethernet_init(void)
{
  s_semph_get_ip_addrs = xSemaphoreCreateBinary();
  if (s_semph_get_ip_addrs == NULL) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

/* tear down connection, release resources */
void ethernet_deinit(void)
{
  if (s_semph_get_ip_addrs == NULL) {
    return;
  }
  vSemaphoreDelete(s_semph_get_ip_addrs);
  s_semph_get_ip_addrs = NULL;

  eth_stop();
}
