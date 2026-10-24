#include "platform.h"

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdio_uart.h"
#include "pico/time.h"

// Cortex-M33 Data Watchpoint and Trace unit
#define DEMCR       (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)

void platform_init() {
  stdio_uart_init();

  // enable the cycle counter used for per-request CPU time
  DEMCR |= (1u << 24);
  DWT_CYCCNT = 0;
  DWT_CTRL |= 1u;

  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, 1);
}

uint64_t platform_now_us(void)
{
  return time_us_64();
}

uint64_t platform_time_ms(void)
{
  // TODO: the board has no synchronised clock yet (SNTP through the USB host);
  // until then this is time since boot, so JWT expiry checks are not meaningful on it
  return time_us_64() / 1000;
}

// extends the 32-bit cycle counter; must be called at least once per wrap (~28 s at 150 MHz)
static uint64_t __cycles(void)
{
  static uint32_t last = 0;
  static uint64_t high = 0;
  uint32_t now = DWT_CYCCNT;
  if (now < last) {
    high += 1ull << 32;
  }
  last = now;
  return high | now;
}

uint64_t platform_now_ns(void)
{
  return __cycles() * 1000000000ull / clock_get_hz(clk_sys);
}

uint64_t platform_task_cpu_ns(void)
{
  // isère runs all request work on the httpd task, and it measures only stretches
  // in which that task does not block, so elapsed cycles equal its CPU time
  return __cycles() * 1000000000ull / clock_get_hz(clk_sys);
}

uint64_t platform_busy_cpu_ns(void)
{
  // all tasks minus the idle task's run time
  uint64_t total = platform_now_us() * 1000ull;
  uint64_t idle = (uint64_t)ulTaskGetIdleRunTimeCounter() * 1000ull;
  return total > idle ? total - idle : 0;
}

size_t platform_heap_size(void)
{
  // heap_3 wraps newlib's malloc, whose arena runs from the end of .bss to the stack limit
  extern char __StackLimit, __bss_end__;
  return (size_t)(&__StackLimit - &__bss_end__);
}
