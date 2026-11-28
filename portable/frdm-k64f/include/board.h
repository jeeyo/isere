#ifndef ISERE_K64F_BOARD_H_
#define ISERE_K64F_BOARD_H_

#include <stdint.h>
#include <stddef.h>

// clocks (120 MHz core from the 50 MHz oscillator), pins, UART0 at 115200 through
// OpenSDA, the PIT time base and the RNGA
void board_init(void);

// 64-bit count of bus-clock (60 MHz) ticks since board_init(), from two chained PIT channels
uint64_t board_ticks(void);
uint32_t board_tick_hz(void);

// 32 random bits from the RNGA
uint32_t board_rand32(void);
void board_random(void *buf, size_t len);

// a locally administered MAC address derived from the chip's unique ID
void board_mac_address(uint8_t mac[6]);

// UART output for printf: queued and sent by the UART interrupt, dropped (and counted)
// when the 4 KB queue is full. The polled variant is for fault reports.
void board_uart_write(const char *buf, size_t len);
void board_uart_write_polled(const char *buf, size_t len);
uint32_t board_uart_dropped(void);

// wall clock: SNTP sets it, platform_time_ms() reads it
void board_set_epoch(uint32_t sec, uint32_t us);
int board_has_epoch(void);

#endif /* ISERE_K64F_BOARD_H_ */
