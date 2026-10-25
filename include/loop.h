#ifndef ISERE_LOOP_H_
#define ISERE_LOOP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "libuv/uv.h"
#include "libuv/queue.h"

// isère's single event loop: socket readiness (poll) plus timers. Everything
// that touches a JS engine, a connection or a timer runs on the task that
// calls isere_loop_run_once(), so none of it needs locking.

typedef struct isere_timer_s isere_timer_t;
typedef void (*isere_timer_cb)(isere_timer_t *timer);

struct isere_timer_s {
  uint64_t deadline_us;
  isere_timer_cb cb;
  void *opaque;
  int active;
  struct uv__queue node;  // in the loop's list, ordered by deadline
};

void isere_loop_init(void);
void isere_loop_deinit(void);
uv_loop_t *isere_loop(void);

void isere_timer_init(isere_timer_t *timer, isere_timer_cb cb, void *opaque);
void isere_timer_start(isere_timer_t *timer, uint64_t delay_ms);
void isere_timer_stop(isere_timer_t *timer);

// waits for sockets or the next timer (at most max_wait_ms), then runs whatever is due
void isere_loop_run_once(int max_wait_ms);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOOP_H_ */
