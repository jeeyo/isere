/*
 * The MIT License (MIT)
 *
 * Based on tinyUSB example that is: Copyright (c) 2020 Peter Lawrence
 * Modified for Pico by Floris Bos
 *
 * influenced by lrndis https://github.com/fetisov/lrndis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb_lwip_glue.h"
#include "pico/unique_id.h"

#include "lwip/tcpip.h"
#include "tusb.h"

#include "FreeRTOS.h"
#include "semphr.h"

/* lwip context */
static struct netif netif_data;

/* shared between tud_network_recv_cb() and service_traffic() */
static QueueHandle_t rxed_queue;

/* this is used by this code, ./class/net/net_driver.c, and usb_descriptors.c */
/* ideally speaking, this should be generated from the hardware's unique ID (if available) */
/* it is suggested that the first byte is 0x02 to indicate a link-local address */
uint8_t tud_network_mac_address[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };

/* network parameters of this MCU */
static const ip_addr_t ipaddr = IPADDR4_INIT_BYTES(192, 168, 7, 1);
static const ip_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
  /* mac ip address                          lease time */
  { {0}, IPADDR4_INIT_BYTES(192, 168, 7, 2), 24 * 60 * 60 },
  { {0}, IPADDR4_INIT_BYTES(192, 168, 7, 3), 24 * 60 * 60 },
  { {0}, IPADDR4_INIT_BYTES(192, 168, 7, 4), 24 * 60 * 60 },
};

static const dhcp_config_t dhcp_config =
{
  .router = IPADDR4_INIT_BYTES(0, 0, 0, 0), /* router address (if any) */
  .port = 67,                               /* listen port */
  .dns = IPADDR4_INIT_BYTES(0, 0, 0, 0),    /* dns server (if any) */
  "usb",                                    /* dns suffix */
  TU_ARRAY_SIZE(entries),                   /* num entry */
  entries                                   /* entries */
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
  (void)netif;

  for (;;)
  {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return ERR_USE;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(p->tot_len))
    {
      tud_network_xmit(p, 0 /* unused for this example */);
      return ERR_OK;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    tud_task();
  }
}

static err_t output_fn(struct netif *netif, struct pbuf *p, const ip_addr_t *addr)
{
  return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput_fn;
  netif->output = output_fn;
  return ERR_OK;
}

static void tcpip_init_done(void *param) {
  lwip_add_netif();
  xSemaphoreGive((SemaphoreHandle_t)param);
}

void lwip_freertos_init(void)
{
  SemaphoreHandle_t init_sem = xSemaphoreCreateBinary();
  tcpip_init(tcpip_init_done, init_sem);
  xSemaphoreTake(init_sem, portMAX_DELAY);
  vSemaphoreDelete(init_sem);
}

void rndis_tusb_init(void)
{
  /* TODO: Fixup MAC address based on flash serial */
  // pico_unique_board_id_t id;
  // pico_get_unique_board_id(&id);
  // memcpy( (tud_network_mac_address)+1, id.id, 5);
  //  Fixing up does not work because tud_network_mac_address is const

  /* Initialize TinyUSB */
  tud_init(BOARD_TUD_RHPORT);

  rxed_queue = xQueueCreate(10, sizeof(struct pbuf *));
}

void lwip_add_netif()
{
  struct netif *netif = &netif_data;

  /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
  netif->hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif->hwaddr[5] ^= 0x01;

  netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, tcpip_input);
  netif_set_default(netif);
}

void tud_network_init_cb(void)
{
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
  if (!size) return true;

  struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
  if (p == NULL) return false;

  /* pbuf_alloc() has already initialized struct; all we need to do is copy the data */
  pbuf_take(p, src, size);

  /* store away the pointer for service_traffic() to later handle */
  struct netif *netif = &netif_data;
  if (netif->input(p, netif) != ERR_OK) {
    pbuf_free(p);
  }

  tud_network_recv_renew();

  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
  struct pbuf *p = (struct pbuf *)ref;

  (void)arg; /* unused for this example */

  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void dhcpd_init()
{
  while (dhserv_init(&dhcp_config) != ERR_OK);
}

void wait_for_netif_is_up()
{
  while (!netif_is_up(&netif_data));
}
