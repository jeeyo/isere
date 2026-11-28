// lwIP driver for the K64F's ENET MAC and the FRDM-K64F's KSZ8081 PHY (RMII, MDIO address 0).
//
// Received frames raise the ENET receive interrupt, which wakes the "eth" task; it copies
// each frame into a pbuf and hands it to lwIP. Transmit copies the frame into a driver
// buffer (fsl_enet's ENET_SendFrame). The same task checks the PHY's link once a second.

#include "ethernetif.h"

#include <string.h>

#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#include "FreeRTOS.h"
#include "task.h"

#include "fsl_enet.h"
#include "board.h"

#define PHY_ADDR            0
#define PHY_BMCR            0x00
#define PHY_BMSR            0x01
#define PHY_ID1             0x02
#define PHY_CTRL1           0x1E
#define PHY_CTRL2           0x1F
#define PHY_BMSR_LINK       0x0004
#define PHY_BMSR_ANEG_DONE  0x0020

#define RX_BD_NUM           4
#define TX_BD_NUM           2
#define BUF_SIZE            SDK_SIZEALIGN(ENET_FRAME_MAX_FRAMELEN, ENET_BUFF_ALIGNMENT)

// the IRQ priority must not be above configMAX_SYSCALL_INTERRUPT_PRIORITY (5)
#define ENET_IRQ_PRIORITY   6
#define ETH_TASK_PRIORITY   (configMAX_PRIORITIES - 2)
#define ETH_TASK_STACK      1024  // words; printf in full newlib needs ~2 KB of stack

SDK_ALIGN(static enet_rx_bd_struct_t __rx_bd[RX_BD_NUM], ENET_BUFF_ALIGNMENT);
SDK_ALIGN(static enet_tx_bd_struct_t __tx_bd[TX_BD_NUM], ENET_BUFF_ALIGNMENT);
SDK_ALIGN(static uint8_t __rx_buf[RX_BD_NUM][BUF_SIZE], ENET_BUFF_ALIGNMENT);
SDK_ALIGN(static uint8_t __tx_buf[TX_BD_NUM][BUF_SIZE], ENET_BUFF_ALIGNMENT);
// frames are assembled here when a pbuf chain has to be flattened
static uint8_t __frame[ENET_FRAME_MAX_FRAMELEN] __attribute__((aligned(4)));

static enet_handle_t __handle;
static TaskHandle_t __task = NULL;
static struct netif *__netif = NULL;

static void __on_enet_event(ENET_Type *base, enet_handle_t *handle, enet_event_t event,
                            enet_frame_info_t *frameInfo, void *userData)
{
  (void)base; (void)handle; (void)frameInfo; (void)userData;
  if (event == kENET_RxEvent && __task != NULL) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(__task, &woken);
    portYIELD_FROM_ISR(woken);
  }
}

static uint16_t __phy_read(uint8_t reg)
{
  uint16_t v = 0xFFFF;
  if (ENET_MDIORead(ENET, PHY_ADDR, reg, &v) != kStatus_Success) {
    return 0xFFFF;
  }
  return v;
}

static err_t __linkoutput(struct netif *netif, struct pbuf *p)
{
  (void)netif;
  const uint8_t *data;
  if (p->next == NULL) {
    data = p->payload;
  } else {
    if (p->tot_len > sizeof(__frame)) {
      return ERR_BUF;
    }
    pbuf_copy_partial(p, __frame, p->tot_len, 0);
    data = __frame;
  }

  // a busy ring frees itself as frames go out; at 100 Mbit/s that takes microseconds
  for (int tries = 0; tries < 2000; tries++) {
    status_t s = ENET_SendFrame(ENET, &__handle, data, p->tot_len, 0, false, NULL);
    if (s == kStatus_Success) {
      return ERR_OK;
    }
    if (s != kStatus_ENET_TxFrameBusy) {
      return ERR_IF;
    }
  }
  return ERR_IF;
}

static void __receive_all(struct netif *netif)
{
  for (;;) {
    uint32_t len = 0;
    status_t s = ENET_GetRxFrameSize(&__handle, &len, 0);
    if (s == kStatus_ENET_RxFrameEmpty) {
      return;
    }
    if (s != kStatus_Success || len == 0 || len > sizeof(__frame)) {
      // drop the frame and give its buffer back to the DMA
      ENET_ReadFrame(ENET, &__handle, NULL, 0, 0, NULL);
      continue;
    }

    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (p == NULL) {
      ENET_ReadFrame(ENET, &__handle, NULL, 0, 0, NULL);
      continue;
    }
    if (p->next == NULL) {
      ENET_ReadFrame(ENET, &__handle, p->payload, len, 0, NULL);
    } else {
      ENET_ReadFrame(ENET, &__handle, __frame, len, 0, NULL);
      pbuf_take(p, __frame, (u16_t)len);
    }
    if (netif->input(p, netif) != ERR_OK) {
      pbuf_free(p);
    }
  }
}

static void __update_link(struct netif *netif)
{
  // BMSR's link bit latches low: the second read is the current state
  (void)__phy_read(PHY_BMSR);
  uint16_t bmsr = __phy_read(PHY_BMSR);
  bool up = bmsr != 0xFFFF && (bmsr & PHY_BMSR_LINK) && (bmsr & PHY_BMSR_ANEG_DONE);

  if (up && !netif_is_link_up(netif)) {
    // operation mode in PHY Control 1, bits 2:0: 1 10HD, 2 100HD, 5 10FD, 6 100FD
    uint16_t mode = __phy_read(PHY_CTRL1) & 0x7;
    enet_mii_speed_t speed = (mode == 2 || mode == 6) ? kENET_MiiSpeed100M : kENET_MiiSpeed10M;
    enet_mii_duplex_t duplex = (mode == 5 || mode == 6) ? kENET_MiiFullDuplex : kENET_MiiHalfDuplex;
    ENET_SetMII(ENET, speed, duplex);
    printf("isere: link up, %s %s duplex\n", speed == kENET_MiiSpeed100M ? "100" : "10",
           duplex == kENET_MiiFullDuplex ? "full" : "half");
    LOCK_TCPIP_CORE();
    netif_set_link_up(netif);
    UNLOCK_TCPIP_CORE();
  } else if (!up && netif_is_link_up(netif)) {
    printf("isere: link down\n");
    LOCK_TCPIP_CORE();
    netif_set_link_down(netif);
    UNLOCK_TCPIP_CORE();
  }
}

static void __eth_task(void *param)
{
  struct netif *netif = param;
  TickType_t last_link_check = 0;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    __receive_all(netif);

    TickType_t now = xTaskGetTickCount();
    if (now - last_link_check >= pdMS_TO_TICKS(1000)) {
      last_link_check = now;
      __update_link(netif);
    }
  }
}

err_t ethernetif_init(struct netif *netif)
{
  __netif = netif;

  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->output = etharp_output;
  netif->linkoutput = __linkoutput;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  board_mac_address(netif->hwaddr);
#if LWIP_NETIF_HOSTNAME
  netif->hostname = "isere";
#endif

  enet_buffer_config_t buffers = {
    .rxBdNumber = RX_BD_NUM,
    .txBdNumber = TX_BD_NUM,
    .rxBuffSizeAlign = BUF_SIZE,
    .txBuffSizeAlign = BUF_SIZE,
    .rxBdStartAddrAlign = __rx_bd,
    .txBdStartAddrAlign = __tx_bd,
    .rxBufferAlign = &__rx_buf[0][0],
    .txBufferAlign = &__tx_buf[0][0],
    .rxMaintainEnable = false,
    .txMaintainEnable = false,
    .txFrameInfo = NULL,
  };

  enet_config_t config;
  ENET_GetDefaultConfig(&config);
  config.miiMode = kENET_RmiiMode;
  config.miiSpeed = kENET_MiiSpeed100M;
  config.miiDuplex = kENET_MiiFullDuplex;
  config.interrupt = kENET_RxFrameInterrupt;
  config.callback = __on_enet_event;
  config.userData = netif;

  NVIC_SetPriority(ENET_Receive_IRQn, ENET_IRQ_PRIORITY);
  NVIC_SetPriority(ENET_Transmit_IRQn, ENET_IRQ_PRIORITY);
  NVIC_SetPriority(ENET_Error_IRQn, ENET_IRQ_PRIORITY);

  if (ENET_Init(ENET, &__handle, &config, &buffers, netif->hwaddr, CLOCK_GetFreq(kCLOCK_CoreSysClk)) != kStatus_Success) {
    printf("isere: ENET_Init failed\n");
    return ERR_IF;
  }

  uint16_t id1 = __phy_read(PHY_ID1);
  printf("isere: PHY id 0x%04x, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", id1,
         netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
         netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]);

  if (xTaskCreate(__eth_task, "eth", ETH_TASK_STACK, netif, ETH_TASK_PRIORITY, &__task) != pdPASS) {
    return ERR_MEM;
  }

  ENET_ActiveRead(ENET);
  return ERR_OK;
}
