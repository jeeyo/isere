#ifndef ISERE_ENGINE_H_
#define ISERE_ENGINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "request.h"
#include "metrics.h"
#include "loop.h"
#include "libuv/queue.h"

// ---------------------------------------------------------------------------
// What httpd sees: src/engine.c, the same for both engines.
//
// Warm mode (the default) keeps one engine instance for the whole process: every
// endpoint's module is evaluated once at boot and each request is a fresh call.
// Cold mode builds an instance for every request and frees it afterwards, which
// is how isère used to work; it stays for comparison and to test cleanup.
// ISERE_JS_MODE=cold|warm selects it on Linux.

int isere_engine_init(void);
void isere_engine_deinit(void);
const char *isere_engine_mode(void);
// runs the garbage collector of the warm instance (for /__metrics?gc=1)
void isere_engine_gc(void);
// bytes the warm instance's engine holds on its heap (0 in cold mode)
size_t isere_engine_heap_used(void);

// starts handling a JS route; the engine queues a response and calls
// isere_request_complete() later. Returns <0 if it could not start.
int isere_engine_dispatch(isere_request_t *req);

// ---------------------------------------------------------------------------
// What each engine binding provides (src/runtimes/<engine>/vm.c)

typedef struct js_vm js_vm_t;

// a stored reference to a JS function, owned by the binding
typedef void *js_ref_t;

// module: load only this app module (cold mode), or -1 for every routed module (warm)
js_vm_t *js_vm_new(int module, uint32_t phase_us[ISERE_PHASE_COUNT]);
// returns the number of objects still alive when the runtime was freed (QuickJS)
uint32_t js_vm_free(js_vm_t *vm);
// heap a module instance keeps (warm mode), measured while loading it
size_t js_vm_module_resident(js_vm_t *vm, int module);

// an instance the engine had to abandon (JerryScript ran out of its heap); src/engine.c
// fails its requests and replaces it
int js_vm_dead(js_vm_t *vm);

void js_vm_enter(js_vm_t *vm);
void js_vm_leave(js_vm_t *vm);

// builds the request object and calls the module's handler through the prelude
int js_vm_dispatch(js_vm_t *vm, isere_request_t *req);
// runs one pending promise job: 1 if one ran, 0 if none was pending, <0 on a fatal error
int js_vm_run_job(js_vm_t *vm);
int js_vm_call(js_vm_t *vm, js_ref_t fn);
void js_vm_release(js_vm_t *vm, js_ref_t fn);
void js_vm_gc(js_vm_t *vm);
// bytes the engine holds (QuickJS: its allocations; JerryScript: used part of its arena)
size_t js_vm_heap_used(js_vm_t *vm);

struct isere_fetch_result_s;
// hands a finished fetch (or its error) to the JS callback `fn`
void js_vm_fetch_done(js_vm_t *vm, js_ref_t fn, const struct isere_fetch_result_s *result);

// ---------------------------------------------------------------------------
// What src/engine.c provides to the bindings' natives

typedef struct {
  uint32_t id;
  js_vm_t *vm;
  uint32_t request_id;    // 0 when not started by a request
  js_ref_t fn;
  uint64_t interval_ms;   // 0 for setTimeout
  isere_timer_t timer;
  struct uv__queue node;
} js_timer_t;

uint32_t isere_js_set_timer(js_vm_t *vm, js_ref_t fn, uint64_t delay_ms, int repeat);
void isere_js_clear_timer(js_vm_t *vm, uint32_t id);

// the request whose code is running now, or NULL
isere_request_t *isere_js_current_request(void);
// looks up a request that is still waiting for its response
isere_request_t *isere_js_find_request(uint32_t id);

// polled by the bindings' interrupt handlers: stop the running script
int isere_js_should_interrupt(void);

// whether the prelude's parts are compiled on first use (ISERE_JS_LAZY, default on)
int isere_js_lazy(void);
// a prelude part by name, or NULL
const isere_app_module_t *isere_js_prelude_part(const char *name, size_t len);

// the app's config (JSON) and the overrides from ISERE_CONFIG (JSON or empty)
const char *isere_js_config(size_t *len);
const char *isere_js_config_overrides(size_t *len);

// console output from JS
void isere_js_log(int level, const char *msg, size_t len);

// calibration helpers (criterion 6)
void isere_js_spin(double ms);
void isere_js_alloc(size_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_ENGINE_H_ */
