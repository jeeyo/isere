#include "board.h"

#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "fsl_port.h"
#include "fsl_uart.h"
#include "fsl_rnga.h"
#include "fsl_sysmpu.h"

#include "clock_config.h"
#include "pin_mux.h"

#define BOARD_UART      UART0
#define BOARD_UART_BAUD 115200

void board_init(void)
{
  BOARD_InitPins();       // UART0 on PTB16/PTB17
  ENET0_InitPins();       // RMII and MDIO
  BOARD_BootClockRUN();   // 120 MHz core, 60 MHz bus

  // the system MPU denies the ENET's DMA by default; isère doesn't use it
  SYSMPU_Enable(SYSMPU, false);

  uart_config_t uart;
  UART_GetDefaultConfig(&uart);
  uart.baudRate_Bps = BOARD_UART_BAUD;
  uart.enableTx = true;
  uart.enableRx = true;
  UART_Init(BOARD_UART, &uart, CLOCK_GetFreq(kCLOCK_CoreSysClk));
  // below the ENET interrupts (6); the handler touches no FreeRTOS state
  NVIC_SetPriority(UART0_RX_TX_IRQn, 7);
  EnableIRQ(UART0_RX_TX_IRQn);

  // PIT channel 1 chained to channel 0: a 64-bit down-counter at the bus clock
  CLOCK_EnableClock(kCLOCK_Pit0);
  // erratum e7914: the first write to MCR right after the clock gate opens can be
  // lost, leaving the module disabled; repeat it until it takes
  do {
    PIT->MCR = 0;  // enabled, keeps running while the debugger halts the core
    __DSB();
  } while (PIT->MCR & PIT_MCR_MDIS_MASK);
  PIT->CHANNEL[0].TCTRL = 0;
  PIT->CHANNEL[1].TCTRL = 0;
  PIT->CHANNEL[1].LDVAL = 0xFFFFFFFFu;
  PIT->CHANNEL[1].TCTRL = PIT_TCTRL_CHN_MASK | PIT_TCTRL_TEN_MASK;
  PIT->CHANNEL[0].LDVAL = 0xFFFFFFFFu;
  PIT->CHANNEL[0].TCTRL = PIT_TCTRL_TEN_MASK;

  RNGA_Init(RNG);
  RNGA_Seed(RNG, SIM->UIDL ^ SIM->UIDML);
}

uint64_t board_ticks(void)
{
  uint32_t hi = PIT->CHANNEL[1].CVAL;
  uint32_t lo = PIT->CHANNEL[0].CVAL;
  uint32_t hi2 = PIT->CHANNEL[1].CVAL;
  if (hi != hi2) {
    // channel 0 wrapped between the reads; its new value goes with the new high word
    lo = PIT->CHANNEL[0].CVAL;
    hi = hi2;
  }
  return ~(((uint64_t)hi << 32) | lo);
}

uint32_t board_tick_hz(void)
{
  return CLOCK_GetFreq(kCLOCK_BusClk);
}

void board_random(void *buf, size_t len)
{
  RNGA_GetRandomData(RNG, buf, len);
}

uint32_t board_rand32(void)
{
  uint32_t r;
  board_random(&r, sizeof(r));
  return r;
}

void board_mac_address(uint8_t mac[6])
{
  uint32_t uidl = SIM->UIDL;
  uint32_t uidml = SIM->UIDML;
  mac[0] = 0x02;  // locally administered, unicast
  mac[1] = (uint8_t)(uidml >> 8);
  mac[2] = (uint8_t)(uidml);
  mac[3] = (uint8_t)(uidl >> 16);
  mac[4] = (uint8_t)(uidl >> 8);
  mac[5] = (uint8_t)(uidl);
}

// UART output is interrupt-driven: writers copy into a ring and return, and the UART
// interrupt drains it. Blocking writes at 115200 baud cost ~11 ms per 130-byte NDJSON
// line, which sat inside every request's latency. When the ring is full, bytes are
// dropped and counted rather than stalling the caller.
#define UART_RING 4096
static volatile uint8_t __tx[UART_RING];
static volatile uint32_t __tx_head = 0;
static volatile uint32_t __tx_tail = 0;
static volatile uint32_t __tx_dropped = 0;

static inline void __tx_put(uint8_t c)
{
  uint32_t next = (__tx_head + 1) % UART_RING;
  if (next == __tx_tail) {
    __tx_dropped++;
    return;
  }
  __tx[__tx_head] = c;
  __tx_head = next;
}

void board_uart_write(const char *buf, size_t len)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      __tx_put('\r');
    }
    __tx_put((uint8_t)buf[i]);
  }
  BOARD_UART->C2 |= UART_C2_TIE_MASK;
  __set_PRIMASK(primask);
}

uint32_t board_uart_dropped(void)
{
  return __tx_dropped;
}

void UART0_RX_TX_IRQHandler(void)
{
  while ((BOARD_UART->S1 & UART_S1_TDRE_MASK) && __tx_tail != __tx_head) {
    BOARD_UART->D = __tx[__tx_tail];
    __tx_tail = (__tx_tail + 1) % UART_RING;
  }
  if (__tx_tail == __tx_head) {
    BOARD_UART->C2 &= ~UART_C2_TIE_MASK;
  }
}

static void __put_polled(uint8_t c)
{
  while ((BOARD_UART->S1 & UART_S1_TDRE_MASK) == 0) {
  }
  BOARD_UART->D = c;
}

// for fault reports: interrupts may be off, so drain the ring and write directly
void board_uart_write_polled(const char *buf, size_t len)
{
  BOARD_UART->C2 &= ~UART_C2_TIE_MASK;
  while (__tx_tail != __tx_head) {
    __put_polled(__tx[__tx_tail]);
    __tx_tail = (__tx_tail + 1) % UART_RING;
  }
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      __put_polled('\r');
    }
    __put_polled((uint8_t)buf[i]);
  }
}
