#include "platform.h"

#include <stdlib.h>
#include <time.h>

static size_t __heap_size = 16 * 1024 * 1024;

void platform_init() {
  // the Linux build has no fixed heap; ISERE_HEAP_SIZE gives it one so that
  // "free heap" and out-of-memory behave like they do on the board
  const char *heap = getenv("ISERE_HEAP_SIZE");
  if (heap != NULL && atol(heap) > 0) {
    __heap_size = (size_t)atol(heap);
  }
}

static uint64_t __clock_ns(clockid_t id)
{
  struct timespec ts;
  clock_gettime(id, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t platform_now_us(void)
{
  return __clock_ns(CLOCK_MONOTONIC) / 1000;
}

uint64_t platform_now_ns(void)
{
  return __clock_ns(CLOCK_MONOTONIC);
}

uint64_t platform_time_ms(void)
{
  return __clock_ns(CLOCK_REALTIME) / 1000000;
}

uint64_t platform_task_cpu_ns(void)
{
  // every FreeRTOS task is a pthread in the POSIX port
  return __clock_ns(CLOCK_THREAD_CPUTIME_ID);
}

uint64_t platform_busy_cpu_ns(void)
{
  return __clock_ns(CLOCK_PROCESS_CPUTIME_ID);
}

size_t platform_heap_size(void)
{
  return __heap_size;
}
