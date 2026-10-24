#ifndef ISERE_PLATFORM_H_
#define ISERE_PLATFORM_H_

#include <stddef.h>
#include <stdint.h>

void platform_init();

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

// bytes of heap the platform reserves for isère, 0 when unbounded
size_t platform_heap_size(void);

#endif /* ISERE_PLATFORM_H_ */
