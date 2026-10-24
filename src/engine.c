#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "mem.h"
#include "platform.h"
#include "fetch.h"

// Every stretch of JS work for a request goes through __run(): it sets the
// request as current (for CPU and allocation accounting, and so setTimeout and
// fetch know who called them), runs the work, then drains the promise jobs it
// queued. The job queue is therefore empty at the start of every stretch, and
// every job that runs belongs to the request whose stretch it is.

#ifndef ISERE_HANDLER_TIMEOUT_MS
#define ISERE_HANDLER_TIMEOUT_MS 5000
#endif

typedef struct {
  isere_request_t *req;
  js_vm_t *vm;
  int owns_vm;            // cold mode: this request's own instance
  struct uv__queue node;  // __inflight
} js_request_t;

static struct {
  int cold;
  js_vm_t *warm;
  uint64_t timeout_ms;
  struct uv__queue inflight;
  struct uv__queue timers;
  uint32_t next_timer_id;
  isere_request_t *current;
  char *overrides;
  size_t overrides_len;
} __e;

static void __finalize(js_request_t *st);
static int __after_run(js_vm_t *vm);

const char *isere_js_config(size_t *len)
{
  *len = isere_app.config_len;
  return isere_app.config;
}

const char *isere_js_config_overrides(size_t *len)
{
  *len = __e.overrides_len;
  return __e.overrides;
}

void isere_js_log(int level, const char *msg, size_t len)
{
  FILE *fp = level >= 2 ? stderr : stdout;
  fputs(level >= 2 ? "[js:error] " : (level == 1 ? "[js:warn] " : "[js] "), fp);
  fwrite(msg, 1, len, fp);
  fputc('\n', fp);
  fflush(fp);
}

void isere_js_spin(double ms)
{
  uint64_t until = platform_now_us() + (uint64_t)(ms * 1000.0);
  volatile uint64_t x = 0;
  while (platform_now_us() < until) {
    x++;
  }
}

void isere_js_alloc(size_t bytes)
{
  // touch the block so the allocation is real
  volatile char *p = isere_malloc(bytes);
  if (p != NULL) {
    for (size_t i = 0; i < bytes; i += 64) p[i] = (char)i;
    isere_free((void *)p);
  }
}

isere_request_t *isere_js_current_request(void)
{
  return __e.current;
}

isere_request_t *isere_js_find_request(uint32_t id)
{
  struct uv__queue *q;
  uv__queue_foreach(q, &__e.inflight) {
    js_request_t *st = uv__queue_data(q, js_request_t, node);
    if (st->req->id == id) {
      return st->req;
    }
  }
  return NULL;
}

int isere_js_should_interrupt(void)
{
  return __e.current != NULL && platform_now_us() >= __e.current->deadline_us;
}

static void __drain(js_vm_t *vm)
{
  // once the deadline passes, the interrupt handler stops each job in turn
  while (js_vm_run_job(vm) > 0) {
  }
}

typedef int (*__work_fn)(js_vm_t *vm, void *arg);

static int __run(js_vm_t *vm, isere_request_t *req, __work_fn fn, void *arg)
{
  isere_request_t *prev = __e.current;
  __e.current = req;
  if (req != NULL) {
    isere_metrics_stretch_begin(&req->metrics);
  }
  uint64_t start = platform_now_us();

  js_vm_enter(vm);
  int r = fn(vm, arg);
  __drain(vm);
  js_vm_leave(vm);

  if (req != NULL) {
    req->metrics.phase_us[ISERE_PHASE_CALL] += (uint32_t)(platform_now_us() - start);
    isere_metrics_stretch_end();
  }
  __e.current = prev;
  return r;
}

static js_request_t *__state(isere_request_t *req)
{
  return req != NULL ? (js_request_t *)req->engine : NULL;
}

// ---------------------------------------------------------------------------
// timers

static void __timer_free(js_timer_t *t)
{
  isere_timer_stop(&t->timer);
  uv__queue_remove(&t->node);
  js_vm_release(t->vm, t->fn);
  isere_free(t);
}

static int __call_timer(js_vm_t *vm, void *arg)
{
  return js_vm_call(vm, (js_ref_t)arg);
}

static void __on_timer(isere_timer_t *timer)
{
  js_timer_t *t = (js_timer_t *)timer->opaque;
  isere_request_t *req = t->request_id ? isere_js_find_request(t->request_id) : NULL;
  js_vm_t *vm = t->vm;
  js_ref_t fn = t->fn;
  int one_shot = t->interval_ms == 0;

  if (one_shot) {
    // a fired timeout is gone; its function is released after the call
    t->fn = NULL;
    __timer_free(t);
  } else {
    // js_vm_call() holds its own reference, so clearInterval() inside the callback is safe
    isere_timer_start(&t->timer, t->interval_ms);
  }

  __run(vm, req, __call_timer, fn);
  if (one_shot) {
    js_vm_release(vm, fn);
  }
  if (__after_run(vm)) {
    // the instance died and took its requests with it; `req` may be gone
    return;
  }

  js_request_t *st = __state(req);
  if (st != NULL && req->responded) {
    __finalize(st);
  }
}

uint32_t isere_js_set_timer(js_vm_t *vm, js_ref_t fn, uint64_t delay_ms, int repeat)
{
  js_timer_t *t = isere_calloc(1, sizeof(js_timer_t));
  if (t == NULL) {
    js_vm_release(vm, fn);
    return 0;
  }
  if (++__e.next_timer_id == 0) {
    __e.next_timer_id = 1;
  }
  t->id = __e.next_timer_id;
  t->vm = vm;
  t->fn = fn;
  t->request_id = __e.current != NULL ? __e.current->id : 0;
  t->interval_ms = repeat ? (delay_ms > 0 ? delay_ms : 1) : 0;
  uv__queue_insert_tail(&__e.timers, &t->node);
  isere_timer_init(&t->timer, __on_timer, t);
  isere_timer_start(&t->timer, delay_ms);
  return t->id;
}

void isere_js_clear_timer(js_vm_t *vm, uint32_t id)
{
  struct uv__queue *q;
  uv__queue_foreach(q, &__e.timers) {
    js_timer_t *t = uv__queue_data(q, js_timer_t, node);
    if (t->id == id && t->vm == vm) {
      __timer_free(t);
      return;
    }
  }
}

// cancels timers belonging to a request (request_id) or to a whole instance (vm)
static void __clear_timers(js_vm_t *vm, uint32_t request_id)
{
  struct uv__queue *q = uv__queue_head(&__e.timers);
  while (q != &__e.timers) {
    js_timer_t *t = uv__queue_data(q, js_timer_t, node);
    q = uv__queue_next(q);
    if ((vm != NULL && t->vm == vm) || (request_id != 0 && t->request_id == request_id)) {
      __timer_free(t);
    }
  }
}

// ---------------------------------------------------------------------------
// requests

static void __finalize(js_request_t *st)
{
  isere_request_t *req = st->req;
  uint64_t start = platform_now_us();
  isere_metrics_stretch_begin(&req->metrics);

  isere_timer_stop(&req->deadline);
  uv__queue_remove(&st->node);
  // what a request leaves running is cancelled when it completes
  __clear_timers(NULL, req->id);
  isere_fetch_cancel_request(req->id);

  if (st->owns_vm) {
    __clear_timers(st->vm, 0);
    isere_fetch_cancel_vm(st->vm);
    uint32_t leftovers = js_vm_free(st->vm);
    isere_metrics_js_leftover_objects(leftovers);
    isere_metrics_js_instance(0);
  }

  req->engine = NULL;
  isere_free(st);

  isere_metrics_stretch_end();
  req->metrics.phase_us[ISERE_PHASE_TEARDOWN] += (uint32_t)(platform_now_us() - start);
  isere_request_complete(req);
}

static void __on_deadline(isere_timer_t *timer)
{
  isere_request_t *req = (isere_request_t *)timer->opaque;
  js_request_t *st = __state(req);
  if (st == NULL) {
    return;
  }
  if (!req->responded) {
    static const char msg[] = "handler timed out\n";
    isere_response_simple(req, 504, "text/plain", msg, sizeof(msg) - 1);
  }
  __finalize(st);
}

static js_vm_t *__new_warm(void)
{
  uint32_t phases[ISERE_PHASE_COUNT] = { 0 };
  js_vm_t *vm = js_vm_new(-1, phases);
  if (vm != NULL) {
    isere_metrics_js_instance(1);
  }
  return vm;
}

// After every stretch: if the instance died (JerryScript out of heap), fail every
// request that was running on it, drop its timers and fetches, and replace it.
// Returns 1 if it did; callers must not touch their request afterwards.
static int __after_run(js_vm_t *vm)
{
  if (!js_vm_dead(vm)) {
    return 0;
  }
  isere_metrics_js_restart();

  int warm = vm == __e.warm;
  if (warm) {
    // new requests go to a fresh instance straight away
    __e.warm = __new_warm();
  }
  __clear_timers(vm, 0);
  isere_fetch_cancel_vm(vm);

  struct uv__queue *q = uv__queue_head(&__e.inflight);
  while (q != &__e.inflight) {
    js_request_t *st = uv__queue_data(q, js_request_t, node);
    q = uv__queue_next(q);
    if (st->vm != vm) {
      continue;
    }
    if (!st->req->responded) {
      static const char msg[] = "the JS engine ran out of memory\n";
      isere_response_simple(st->req, 500, "text/plain", msg, sizeof(msg) - 1);
    }
    // finalizing may complete other requests and change the list, so start over
    __finalize(st);
    q = uv__queue_head(&__e.inflight);
  }

  if (warm) {
    js_vm_free(vm);
    isere_metrics_js_instance(0);
  }
  return 1;
}

static int __dispatch(js_vm_t *vm, void *arg)
{
  return js_vm_dispatch(vm, (isere_request_t *)arg);
}

int isere_engine_dispatch(isere_request_t *req)
{
  const isere_app_route_t *route = &isere_app.routes[req->route];

  js_request_t *st = isere_calloc(1, sizeof(js_request_t));
  if (st == NULL) {
    return -1;
  }
  st->req = req;
  req->engine = st;
  req->deadline_us = platform_now_us() + __e.timeout_ms * 1000;

  if (__e.cold) {
    isere_metrics_stretch_begin(&req->metrics);
    st->vm = js_vm_new(route->module, req->metrics.phase_us);
    isere_metrics_stretch_end();
    if (st->vm == NULL) {
      req->engine = NULL;
      isere_free(st);
      return -1;
    }
    st->owns_vm = 1;
    isere_metrics_js_instance(1);
  } else {
    if (__e.warm == NULL) {
      __e.warm = __new_warm();
    }
    if (__e.warm == NULL) {
      req->engine = NULL;
      isere_free(st);
      return -1;
    }
    st->vm = __e.warm;
  }

  uv__queue_insert_tail(&__e.inflight, &st->node);
  isere_timer_init(&req->deadline, __on_deadline, req);
  isere_timer_start(&req->deadline, __e.timeout_ms);

  int r = __run(st->vm, req, __dispatch, req);
  if (__after_run(st->vm)) {
    return 0;
  }
  if (!req->responded && platform_now_us() >= req->deadline_us) {
    // the handler ran past its deadline and was interrupted
    static const char msg[] = "handler timed out\n";
    isere_response_simple(req, 504, "text/plain", msg, sizeof(msg) - 1);
  } else if (!req->responded && r < 0) {
    static const char msg[] = "handler failed\n";
    isere_response_simple(req, 500, "text/plain", msg, sizeof(msg) - 1);
  }

  if (req->responded) {
    __finalize(st);
  }
  return 0;
}

// a fetch the engine started has finished; deliver it on behalf of its request
typedef struct {
  js_ref_t fn;
  const isere_fetch_result_t *result;
} __fetch_arg_t;

static int __deliver_fetch(js_vm_t *vm, void *arg)
{
  __fetch_arg_t *a = (__fetch_arg_t *)arg;
  js_vm_fetch_done(vm, a->fn, a->result);
  return 0;
}

void isere_engine_fetch_done(js_vm_t *vm, uint32_t request_id, js_ref_t fn, const isere_fetch_result_t *result)
{
  isere_request_t *req = request_id ? isere_js_find_request(request_id) : NULL;
  __fetch_arg_t arg = { fn, result };
  __run(vm, req, __deliver_fetch, &arg);
  js_vm_release(vm, fn);
  if (__after_run(vm)) {
    return;
  }

  js_request_t *st = __state(req);
  if (st != NULL && req->responded) {
    __finalize(st);
  }
}

void isere_engine_gc(void)
{
  if (__e.warm != NULL) {
    js_vm_gc(__e.warm);
  }
}

const char *isere_engine_mode(void)
{
  return __e.cold ? "cold" : "warm";
}

int isere_engine_init(void)
{
  memset(&__e, 0, sizeof(__e));
  uv__queue_init(&__e.inflight);
  uv__queue_init(&__e.timers);

  const char *mode = getenv("ISERE_JS_MODE");
  __e.cold = mode != NULL && strcmp(mode, "cold") == 0;
  isere_metrics_js_mode(isere_engine_mode());

  __e.timeout_ms = ISERE_HANDLER_TIMEOUT_MS;
  const char *timeout = getenv("ISERE_HANDLER_TIMEOUT_MS");
  if (timeout != NULL && atol(timeout) > 0) {
    __e.timeout_ms = (uint64_t)atol(timeout);
  }

  const char *overrides = getenv("ISERE_CONFIG");
  if (overrides != NULL) {
    size_t len = 0;
    while (overrides[len] != '\0') len++;
    __e.overrides = isere_malloc(len + 1);
    if (__e.overrides != NULL) {
      memcpy(__e.overrides, overrides, len + 1);
      __e.overrides_len = len;
    }
  }

  if (!__e.cold) {
    uint32_t phases[ISERE_PHASE_COUNT] = { 0 };
    __e.warm = js_vm_new(-1, phases);
    if (__e.warm == NULL) {
      return -1;
    }
    isere_metrics_js_instance(1);
    for (int i = 0; i < ISERE_PHASE_COUNT; i++) {
      isere_metrics_mark_phase_boot((isere_phase_t)i, phases[i]);
    }
    for (uint32_t i = 0; i < isere_app.num_routes; i++) {
      if (isere_app.routes[i].kind == ISERE_ROUTE_JS) {
        isere_metrics_route_resident((int)i, js_vm_module_resident(__e.warm, isere_app.routes[i].module));
      }
    }
  }
  return 0;
}

void isere_engine_deinit(void)
{
  // requests still in flight at shutdown are dropped without an answer
  while (!uv__queue_empty(&__e.inflight)) {
    js_request_t *st = uv__queue_data(uv__queue_head(&__e.inflight), js_request_t, node);
    if (!st->req->responded) {
      static const char msg[] = "shutting down\n";
      isere_response_simple(st->req, 503, "text/plain", msg, sizeof(msg) - 1);
    }
    __finalize(st);
  }

  if (__e.warm != NULL) {
    __clear_timers(__e.warm, 0);
    isere_fetch_cancel_vm(__e.warm);
    uint32_t leftovers = js_vm_free(__e.warm);
    isere_metrics_js_leftover_objects(leftovers);
    isere_metrics_js_instance(0);
    __e.warm = NULL;
    if (leftovers > 0) {
      fprintf(stderr, "[js] %u objects were still alive when the runtime was freed\n", leftovers);
    }
  }

  isere_free(__e.overrides);
  __e.overrides = NULL;
}
