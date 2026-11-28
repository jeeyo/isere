#include "platform.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void platform_exit(int code)
{
  // Exits straight from the task instead of unwinding the FreeRTOS POSIX port
  // (vTaskEndScheduler), which cancels and joins the port's task threads and can
  // hang under valgrind on x86-64. The tick signal is blocked so no context
  // switch interrupts the exit.
  sigset_t all;
  sigfillset(&all);
  pthread_sigmask(SIG_BLOCK, &all, NULL);
  fflush(NULL);
  exit(code);
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

void *platform_malloc(size_t size)
{
  return malloc(size);
}

void platform_free(void *ptr)
{
  free(ptr);
}

size_t platform_heap_size(void)
{
  return __heap_size;
}

// The POSIX port runs each task on a pthread whose stack FreeRTOS never sees, so
// the high-water mark is found the classic way: paint the unused part of the
// stack below the current frame, and later find the deepest byte that changed.
#define STACK_PATTERN 0xA5
#define STACK_MARGIN 1024

static volatile uint8_t *__watch_low = NULL;
static volatile uint8_t *__watch_top = NULL;

__attribute__((noinline)) void platform_stack_watch(size_t stack_bytes)
{
  // memcheck rightly reports writes below the stack pointer; under valgrind,
  // which preloads its own library, the mark is simply not available
  const char *preload = getenv("LD_PRELOAD");
  if (preload != NULL && strstr(preload, "vgpreload") != NULL) {
    return;
  }

  volatile uint8_t *sp = (volatile uint8_t *)__builtin_frame_address(0);
  // stay clear of the guard page and of this frame
  size_t depth = stack_bytes > 2 * 8192 ? stack_bytes - 8192 : stack_bytes / 2;
  __watch_top = sp;
  __watch_low = sp - depth;
  for (volatile uint8_t *p = __watch_low; p < sp - STACK_MARGIN; p++) {
    *p = STACK_PATTERN;
  }
}

size_t platform_stack_used(void)
{
  if (__watch_low == NULL) {
    return 0;
  }
  volatile uint8_t *p = __watch_low;
  while (p < __watch_top - STACK_MARGIN && *p == STACK_PATTERN) {
    p++;
  }
  // measured from the frame that started watching, near the top of the task
  return (size_t)(__watch_top - p);
}
