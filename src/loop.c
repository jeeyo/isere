#include "loop.h"

#include <string.h>

#include "platform.h"

static uv_loop_t __loop;
static struct uv__queue __timers;

void isere_loop_init(void)
{
  memset(&__loop, 0, sizeof(__loop));
  uv__platform_loop_init(&__loop);
  uv__queue_init(&__loop.watcher_queue);
  uv__queue_init(&__timers);
}

void isere_loop_deinit(void)
{
  uv__platform_loop_delete(&__loop);
  uv__platform_loop_free_watchers(&__loop);
}

uv_loop_t *isere_loop(void)
{
  return &__loop;
}

void isere_timer_init(isere_timer_t *timer, isere_timer_cb cb, void *opaque)
{
  memset(timer, 0, sizeof(*timer));
  timer->cb = cb;
  timer->opaque = opaque;
  uv__queue_init(&timer->node);
}

void isere_timer_start(isere_timer_t *timer, uint64_t delay_ms)
{
  isere_timer_stop(timer);
  timer->deadline_us = platform_now_us() + delay_ms * 1000;
  timer->active = 1;

  // a handful of timers at most, so a sorted list is enough
  struct uv__queue *q;
  uv__queue_foreach(q, &__timers) {
    isere_timer_t *t = uv__queue_data(q, isere_timer_t, node);
    if (t->deadline_us > timer->deadline_us) {
      uv__queue_insert_tail(q, &timer->node);
      return;
    }
  }
  uv__queue_insert_tail(&__timers, &timer->node);
}

void isere_timer_stop(isere_timer_t *timer)
{
  if (!timer->active) {
    return;
  }
  uv__queue_remove(&timer->node);
  uv__queue_init(&timer->node);
  timer->active = 0;
}

void isere_loop_run_once(int max_wait_ms)
{
  int timeout = max_wait_ms;
  if (!uv__queue_empty(&__timers)) {
    isere_timer_t *next = uv__queue_data(uv__queue_head(&__timers), isere_timer_t, node);
    uint64_t now = platform_now_us();
    uint64_t wait_ms = next->deadline_us > now ? (next->deadline_us - now + 999) / 1000 : 0;
    if (wait_ms < (uint64_t)timeout) {
      timeout = (int)wait_ms;
    }
  }

  uv__io_poll(&__loop, timeout);

  // run due timers; a callback may start or stop any timer, so re-read the head each time
  uint64_t now = platform_now_us();
  while (!uv__queue_empty(&__timers)) {
    isere_timer_t *t = uv__queue_data(uv__queue_head(&__timers), isere_timer_t, node);
    if (t->deadline_us > now) {
      break;
    }
    isere_timer_stop(t);
    t->cb(t);
  }
}
