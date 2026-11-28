// newlib glue for the FRDM-K64F: stdout/stderr to UART0, a bounded _sbrk for
// newlib's own small heap, malloc locking against the scheduler, and loud failures.

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <reent.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fsl_device_registers.h"
#include "board.h"
#include "platform.h"

// monotonic and "real" time are both time since boot until SNTP sets a wall clock
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  (void)clock_id;
  uint64_t ns = platform_now_ns();
  tp->tv_sec = (time_t)(ns / 1000000000ull);
  tp->tv_nsec = (long)(ns % 1000000000ull);
  return 0;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
  (void)tz;
  uint64_t us = platform_now_us();
  tv->tv_sec = (time_t)(us / 1000000ull);
  tv->tv_usec = (suseconds_t)(us % 1000000ull);
  return 0;
}

int _write(int fd, const char *buf, int len)
{
  (void)fd;
  board_uart_write(buf, (size_t)len);
  return len;
}

int _read(int fd, char *buf, int len)
{
  (void)fd; (void)buf; (void)len;
  errno = ENOSYS;
  return -1;
}

// newlib's allocator (stdio buffers, dtoa's big integers for printf("%f"), strdup...)
// shares FreeRTOS's heap_5 with everything else: a fixed newlib heap is one more
// reserve to size, and dtoa ran a 4 KB one out. pvPortMalloc is thread-safe. Each
// block carries its size for realloc, in 8 bytes to keep the payload 8-aligned.
#define NEWLIB_HDR 8

void *_malloc_r(struct _reent *r, size_t n)
{
  (void)r;
  uint8_t *p = pvPortMalloc(n + NEWLIB_HDR);
  if (p == NULL) {
    return NULL;
  }
  *(size_t *)p = n;
  return p + NEWLIB_HDR;
}

void _free_r(struct _reent *r, void *ptr)
{
  (void)r;
  if (ptr != NULL) {
    vPortFree((uint8_t *)ptr - NEWLIB_HDR);
  }
}

void *_calloc_r(struct _reent *r, size_t count, size_t size)
{
  if (size != 0 && count > (size_t)-1 / size) {
    return NULL;
  }
  void *p = _malloc_r(r, count * size);
  if (p != NULL) {
    memset(p, 0, count * size);
  }
  return p;
}

void *_realloc_r(struct _reent *r, void *ptr, size_t n)
{
  if (ptr == NULL) {
    return _malloc_r(r, n);
  }
  if (n == 0) {
    _free_r(r, ptr);
    return NULL;
  }
  size_t old = *(size_t *)((uint8_t *)ptr - NEWLIB_HDR);
  void *p = _malloc_r(r, n);
  if (p != NULL) {
    memcpy(p, ptr, old < n ? old : n);
    _free_r(r, ptr);
  }
  return p;
}

void *malloc(size_t n) { return _malloc_r(_REENT, n); }
void free(void *ptr) { _free_r(_REENT, ptr); }
void *calloc(size_t count, size_t size) { return _calloc_r(_REENT, count, size); }
void *realloc(void *ptr, size_t n) { return _realloc_r(_REENT, ptr, n); }

// nothing should grow newlib's own heap any more; fail loudly if something does
void *_sbrk(ptrdiff_t incr)
{
  (void)incr;
  errno = ENOMEM;
  return (void *)-1;
}

static void __print(const char *s)
{
  board_uart_write_polled(s, strlen(s));
}

// no libc here: the fault may have been in it
static void __print_hex(const char *label, uint32_t v)
{
  char buf[16];
  buf[0] = '=';
  buf[1] = '0';
  buf[2] = 'x';
  for (int i = 0; i < 8; i++) {
    buf[3 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 0xF];
  }
  buf[11] = ' ';
  __print(label);
  board_uart_write_polled(buf, 12);
}

__attribute__((noreturn)) static void __halt(void)
{
  __disable_irq();
  for (;;) {
    __BKPT(0);
  }
}

void _exit(int code)
{
  char buf[48];
  snprintf(buf, sizeof(buf), "\nisere: halted (exit %d)\n", code);
  __print(buf);
  __halt();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
  (void)task;
  __print("\nisere: stack overflow in task ");
  __print(name);
  __print("\n");
  __halt();
}

// called by HardFault_Handler with the stacked exception frame
void board_fault(uint32_t *frame, uint32_t exc_return)
{
  __print("\nisere: hard fault ");
  __print_hex("pc", frame[6]);
  __print_hex("lr", frame[5]);
  __print_hex("psr", frame[7]);
  __print_hex("r0", frame[0]);
  __print_hex("r1", frame[1]);
  __print_hex("r12", frame[4]);
  __print_hex("exc", exc_return);
  __print_hex("cfsr", SCB->CFSR);
  __print_hex("hfsr", SCB->HFSR);
  __print_hex("mmfar", SCB->MMFAR);
  __print_hex("bfar", SCB->BFAR);
  __print_hex("ipsr", __get_IPSR());
  __print_hex("dfsr", SCB->DFSR);
  __print_hex("demcr", CoreDebug->DEMCR);
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    __print("task ");
    __print(pcTaskGetName(NULL));
  }
  __print("\n");
  __halt();
}

#define FAULT_ENTRY \
  __asm volatile(            \
    "tst lr, #4      \n"     \
    "ite eq          \n"     \
    "mrseq r0, msp   \n"     \
    "mrsne r0, psp   \n"     \
    "mov r1, lr      \n"     \
    "b board_fault   \n")

// every unexpected exception reports itself the same way
__attribute__((naked)) void DefaultISR(void) { FAULT_ENTRY; }
// a debugger that detaches (OpenOCD's shutdown) can leave a debug monitor request
// pending; it is not a fault, so clear it and carry on
void DebugMon_Handler(void)
{
  CoreDebug->DEMCR &= ~CoreDebug_DEMCR_MON_PEND_Msk;
}
__attribute__((naked)) void MemManage_Handler(void) { FAULT_ENTRY; }
__attribute__((naked)) void BusFault_Handler(void) { FAULT_ENTRY; }
__attribute__((naked)) void UsageFault_Handler(void) { FAULT_ENTRY; }

__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile(
    "tst lr, #4      \n"
    "ite eq          \n"
    "mrseq r0, msp   \n"
    "mrsne r0, psp   \n"
    "mov r1, lr      \n"
    "b board_fault   \n");
}
