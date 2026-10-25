#ifndef ISERE_PLATFORM_H_
#define ISERE_PLATFORM_H_

#include <stddef.h>
#include <stdint.h>

void platform_init();

// Ends the program once isère has shut down and freed what it allocated (Linux);
// returns on platforms that never exit (the board)
void platform_exit(int code);

// monotonic wall clock, microseconds / nanoseconds since an arbitrary point
uint64_t platform_now_us(void);
uint64_t platform_now_ns(void);

// wall-clock time, milliseconds since the Unix epoch
uint64_t platform_time_ms(void);

// CPU time consumed by the calling task, nanoseconds.
// Linux: the thread's CPU clock. Pico 2: the DWT cycle counter scaled to ns;
// accurate only while the calling task is the one running.
uint64_t platform_task_cpu_ns(void);

// CPU time consumed by the whole device (all tasks except idle), nanoseconds
uint64_t platform_busy_cpu_ns(void);

// The heap underneath isère's accounting allocator (src/mem.c). Linux: the C
// library's malloc, which is thread-safe on its own; FreeRTOS's heap_3 would
// wrap every call in a critical section, two signal-mask syscalls in the POSIX
// port. Pico 2: pvPortMalloc, since other tasks share the heap.
void *platform_malloc(size_t size);
void platform_free(void *ptr);

// bytes of heap the platform reserves for isère, 0 when unbounded
size_t platform_heap_size(void);

// Stack high-water mark of the calling task. platform_stack_watch() starts
// watching (call it first thing in the task, with the task's stack size in
// bytes); platform_stack_used() is the most it has used since, in bytes.
void platform_stack_watch(size_t stack_bytes);
size_t platform_stack_used(void);

#endif /* ISERE_PLATFORM_H_ */
