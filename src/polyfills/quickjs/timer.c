#include "polyfills.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

#include "FreeRTOS.h"
#include "timers.h"

JSClassID polyfill_timer_class_id;

static polyfill_timer_t *__polyfill_timer_get_free_slot(polyfill_timer_t timers[])
{
  for (int i = 0; i < ISERE_JS_POLYFILLS_MAX_TIMERS; i++) {
    if (timers[i].timer == NULL) {
      return &timers[i];
    }
  }

  return NULL;
}

static void polyfill_timer_callback(TimerHandle_t timer)
{
  polyfill_timer_t *tmr = (polyfill_timer_t *)pvTimerGetTimerID(timer);
  JSContext *ctx = tmr->ctx;
  JSValueConst func = tmr->func;

  JSValue ret, func1;
  /* 'func' might be destroyed when calling itself (if it frees the
      handler), so must take extra care */
  func1 = JS_DupValue(ctx, func);
  ret = JS_Call(ctx, func1, JS_UNDEFINED, 0, NULL);
  JS_FreeValue(ctx, func1);
  if (JS_IsException(ret)) {
    js_std_dump_error(ctx);
  }
  JS_FreeValue(ctx, ret);

  JS_FreeValue(ctx, func);
  tmr->timer = NULL;
  tmr->func = JS_UNDEFINED;
}

JSValue polyfill_timer_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  int64_t delay;
  JSValueConst func;
  JSValue obj;

  func = argv[0];
  if (!JS_IsFunction(ctx, func)) {
    return JS_ThrowTypeError(ctx, "not a function");
  }
  if (JS_ToInt64(ctx, &delay, argv[1]) < 0) {
    return JS_ThrowTypeError(ctx, "not a number");
  }
  obj = JS_NewObjectClass(ctx, polyfill_timer_class_id);
  if (JS_IsException(obj)) {
    return obj;
  }

  isere_js_context_t *js = (isere_js_context_t *)JS_GetContextOpaque(ctx);
  polyfill_timer_t *tmr = __polyfill_timer_get_free_slot(js->timers);
  if (!tmr) {
    JS_FreeValue(ctx, obj);
    return JS_ThrowInternalError(ctx, "exceeded timer limit");
  }

  tmr->ctx = ctx;
  tmr->func = JS_DupValue(ctx, func);

  TimerHandle_t timer = xTimerCreate("setTimeout", delay / portTICK_PERIOD_MS, pdFALSE, (void *)tmr, polyfill_timer_callback);
  if (!timer) {
    JS_FreeValue(ctx, obj);
    return JS_ThrowInternalError(ctx, "insufficient memory");
  }

  tmr->timer = timer;

  if (xTimerStart(timer, 0) != pdPASS) {
    JS_FreeValue(ctx, obj);
    return JS_ThrowInternalError(ctx, "timer could not be started");
  }

  JS_SetOpaque(obj, timer);
  return obj;
}

JSValue polyfill_timer_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  TimerHandle_t timer = JS_GetOpaque2(ctx, argv[0], polyfill_timer_class_id);
  if (!timer) {
    return JS_ThrowInternalError(ctx, "timer could not be found");
  }

  xTimerStop(timer, 0);
  return JS_UNDEFINED;
}

void isere_js_polyfill_timer_init(isere_js_context_t *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  JS_SetPropertyStr(ctx->context, global_obj, "setTimeout", JS_NewCFunction(ctx->context, polyfill_timer_setTimeout, "setTimeout", 2));
  JS_SetPropertyStr(ctx->context, global_obj, "clearTimeout", JS_NewCFunction(ctx->context, polyfill_timer_clearTimeout, "clearTimeout", 1));
  JS_FreeValue(ctx->context, global_obj);

  for (int i = 0; i < ISERE_JS_POLYFILLS_MAX_TIMERS; i++) {
    polyfill_timer_t *tmr = &ctx->timers[i];
    memset(tmr, 0, sizeof(polyfill_timer_t));
    tmr->timer = NULL;
    tmr->ctx = ctx->context;
    tmr->func = JS_UNDEFINED;
  }
}

void isere_js_polyfill_timer_deinit(isere_js_context_t *ctx)
{
  for (int i = 0; i < ISERE_JS_POLYFILLS_MAX_TIMERS; i++) {
    polyfill_timer_t *tmr = &ctx->timers[i];

    if (tmr->timer != NULL) {
      TimerHandle_t timer = tmr->timer;
      xTimerStop(timer, 0);
      xTimerDelete(timer, 0);
    }

    JSContext *ctx = tmr->ctx;
    JSValueConst func = tmr->func;

    JS_FreeValue(ctx, func);
  }

  JSValue global_obj = JS_GetGlobalObject(ctx->context);

  JSAtom setTimeout = JS_NewAtom(ctx->context, "setTimeout");
  JS_DeleteProperty(ctx->context, global_obj, setTimeout, 0);
  JS_FreeAtom(ctx->context, setTimeout);

  JSAtom clearTimeout = JS_NewAtom(ctx->context, "clearTimeout");
  JS_DeleteProperty(ctx->context, global_obj, clearTimeout, 0);
  JS_FreeAtom(ctx->context, clearTimeout);

  JS_FreeValue(ctx->context, global_obj);
}

int isere_js_polyfill_timer_poll(isere_js_context_t *ctx)
{
  for (int i = 0; i < ISERE_JS_POLYFILLS_MAX_TIMERS; i++) {
    polyfill_timer_t *tmr = &ctx->timers[i];
    TimerHandle_t timer = tmr->timer;
    if (timer != NULL && xTimerIsTimerActive(timer)) {
      return 1;
    }
  }

  return 0;
}
