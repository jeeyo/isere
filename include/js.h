#ifndef ISERE_JS_H_
#define ISERE_JS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "timers.h"

#ifdef ISERE_RUNTIME_QUICKJS
#include "quickjs.h"
#endif /* ISERE_RUNTIME_QUICKJS */

#ifdef ISERE_RUNTIME_JERRYSCRIPT
#include "jerryscript.h"
#endif /* ISERE_RUNTIME_JERRYSCRIPT */

#define ISERE_JS_LOG_TAG "js"

#define ISERE_JS_POLYFILLS_MAX_TIMERS 5

typedef struct {
  TimerHandle_t timer;
#ifdef ISERE_RUNTIME_QUICKJS
  JSContext *ctx;
  JSValue func;
#endif /* ISERE_RUNTIME_QUICKJS */
} polyfill_timer_t;

typedef struct {
  uint8_t initialized;
#ifdef ISERE_RUNTIME_QUICKJS
  JSRuntime *runtime;
  JSContext *context;
  JSValue future;
#endif /* ISERE_RUNTIME_QUICKJS */
#ifdef ISERE_RUNTIME_JERRYSCRIPT
  jerry_context_t *context;
#endif /* ISERE_RUNTIME_JERRYSCRIPT */
  polyfill_timer_t timers[ISERE_JS_POLYFILLS_MAX_TIMERS];
  void *opaque;
} isere_js_context_t;

typedef void * isere_js_t;

int isere_js_init(isere_js_t *js);
int isere_js_deinit(isere_js_t *js);
int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx);
int isere_js_free_context(isere_js_t *js, isere_js_context_t *ctx);
int isere_js_eval(
  isere_js_context_t *ctx,
  unsigned char *handler,
  unsigned int handler_len,
  const char *method,
  const char *path,
  const char *query,
  const char (*request_header_names)[],
  const char (*request_header_values)[],
  const uint32_t request_headers_len,
  const char *body);
int isere_js_poll(isere_js_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_JS_H_ */
