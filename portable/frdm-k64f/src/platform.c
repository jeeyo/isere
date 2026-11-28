#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "app.h"

// heap_5 regions from the linker script: all of SRAM_L, and SRAM_U between
// newlib's small heap and the MSP stack
extern uint8_t __sram_l_start[], __sram_l_end[], __HeapLimit[], __StackLimit[];
static size_t __heap_size = 0;

void platform_init()
{
  board_init();

  HeapRegion_t regions[] = {
    { __sram_l_start, (size_t)(__sram_l_end - __sram_l_start) },
    { __HeapLimit, (size_t)(__StackLimit - __HeapLimit) },
    { NULL, 0 },
  };
  vPortDefineHeapRegions(regions);
  __heap_size = regions[0].xSizeInBytes + regions[1].xSizeInBytes;

  // unbuffered: a crash report must not be lost in a buffer, and no buffer is allocated
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  // "NAME=value;NAME=value" from the build (ISERE_BOARD_ENV)
  static char env[] = ISERE_BOARD_ENV;
  for (char *item = strtok(env, ";"); item != NULL; item = strtok(NULL, ";")) {
    char *eq = strchr(item, '=');
    if (eq != NULL) {
      *eq = '\0';
      setenv(item, eq + 1, 1);
    }
  }

  printf("\nisere: FRDM-K64F, %lu MHz, build %s, heap %u bytes\n",
         (unsigned long)(SystemCoreClock / 1000000), isere_build_id, (unsigned)__heap_size);
}

void platform_exit(int code)
{
  // firmware doesn't exit; the httpd task just ends
  (void)code;
}

uint64_t platform_now_ns(void)
{
  // 60 MHz bus clock: 50/3 ns per tick
  uint64_t t = board_ticks();
  uint32_t hz = board_tick_hz();
  return (t / hz) * 1000000000ull + (t % hz) * 1000000000ull / hz;
}

uint64_t platform_now_us(void)
{
  uint64_t t = board_ticks();
  uint32_t hz = board_tick_hz();
  return (t / hz) * 1000000ull + (t % hz) * 1000000ull / hz;
}

// Unix time minus time since boot, in microseconds; set by SNTP (lwipopts.h)
static volatile int64_t __epoch_offset_us = 0;

void board_set_epoch(uint32_t sec, uint32_t us)
{
  __epoch_offset_us = (int64_t)sec * 1000000 + us - (int64_t)platform_now_us();
}

int board_has_epoch(void)
{
  return __epoch_offset_us != 0;
}

uint64_t platform_time_ms(void)
{
  // time since boot until SNTP has answered (tcp.c waits for it before serving)
  return (uint64_t)((int64_t)platform_now_us() + __epoch_offset_us) / 1000;
}

uint64_t platform_task_cpu_ns(void)
{
  // isère runs all request work on the httpd task, and it measures only stretches
  // in which that task does not block, so elapsed time equals its CPU time
  return platform_now_ns();
}

uint64_t platform_busy_cpu_ns(void)
{
  // all tasks minus the idle task's run time
  uint64_t total = platform_now_us() * 1000ull;
  uint64_t idle = (uint64_t)ulTaskGetIdleRunTimeCounter() * 1000ull;
  return total > idle ? total - idle : 0;
}

void *platform_malloc(size_t size)
{
  return pvPortMalloc(size);
}

void platform_free(void *ptr)
{
  vPortFree(ptr);
}

size_t platform_heap_size(void)
{
  return __heap_size;
}

static TaskHandle_t __watched = NULL;
static size_t __watched_bytes = 0;

void platform_stack_watch(size_t stack_bytes)
{
  // FreeRTOS fills new task stacks with a known value; the kernel reports the mark
  __watched = xTaskGetCurrentTaskHandle();
  __watched_bytes = stack_bytes;
}

size_t platform_stack_used(void)
{
  if (__watched == NULL) {
    return 0;
  }
  size_t free_bytes = (size_t)uxTaskGetStackHighWaterMark(__watched) * sizeof(StackType_t);
  return __watched_bytes > free_bytes ? __watched_bytes - free_bytes : 0;
}

// mbedTLS entropy (MBEDTLS_ENTROPY_HARDWARE_ALT): the RNGA
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
  (void)data;
  board_random(output, len);
  *olen = len;
  return 0;
}
