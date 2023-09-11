#include "polyfills.h"

#include "quickjs.h"
#include "quickjs-libc.h"

#include "FreeRTOS.h"
#include "timers.h"
#include "klist.h"

JSClassID polyfill_timer_class_id;

static struct list_head timers;

typedef struct {
  JSContext *ctx;
  JSValue func;
} polyfill_timer_data_t;

static void polyfill_timer_callback(TimerHandle_t timer)
{
  polyfill_timer_data_t *params = (polyfill_timer_data_t *)pvTimerGetTimerID(timer);
  JSContext *ctx = params->ctx;
  JSValueConst func = params->func;

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
}

void polyfill_timer_init(JSContext *ctx)
{
  init_list_head(&timers);
}

void polyfill_timer_deinit(JSContext *ctx)
{
  struct list_head *el, *el1;

  list_for_each_safe(el, el1, &timers) {
    polyfill_timer_t *tmr = list_entry(el, polyfill_timer_t, link);
    xTimerHandle timer = tmr->timer;
    xTimerStop(timer, 0);

    polyfill_timer_data_t *params = (polyfill_timer_data_t *)pvTimerGetTimerID(timer);
    JSContext *ctx = params->ctx;
    JSValueConst func = params->func;

    JS_FreeValue(ctx, func);
    free(params);
    xTimerDelete(timer, 0);
  }

  list_for_each_safe(el, el1, &timers) {
    polyfill_timer_t *tmr = list_entry(el, polyfill_timer_t, link);
    list_del(&tmr->link);
    free(tmr);
  }
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
  if (JS_ToInt64(ctx, &delay, argv[1])) {
    return JS_EXCEPTION;
  }
  obj = JS_NewObjectClass(ctx, polyfill_timer_class_id);
  if (JS_IsException(obj)) {
    return obj;
  }

  polyfill_timer_data_t *params = (polyfill_timer_data_t *)malloc(sizeof(polyfill_timer_data_t));
  params->ctx = ctx;
  params->func = JS_DupValue(ctx, func);

  TimerHandle_t timer = xTimerCreate("setTimeout", delay / portTICK_PERIOD_MS, pdFALSE, (void *)params, polyfill_timer_callback);
  if (!timer) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  polyfill_timer_t *tmr = (polyfill_timer_t *)malloc(sizeof(polyfill_timer_t));
  tmr->timer = timer;
  list_add_tail(&tmr->link, &timers);

  if (xTimerStart(timer, 0) != pdPASS) {
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
  }

  JS_SetOpaque(obj, timer);
  return obj;
}

JSValue polyfill_timer_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  TimerHandle_t timer = JS_GetOpaque2(ctx, argv[0], polyfill_timer_class_id);
  if (!timer) {
    return JS_EXCEPTION;
  }

  xTimerStop(timer, 0);
  return JS_UNDEFINED;
}

int polyfill_timer_poll(JSContext *ctx)
{
  struct list_head *el;

  if (list_empty(&timers)) {
    return 1;
  }

  for (;;) {
poll:
    vTaskDelay(50 / portTICK_PERIOD_MS);

    list_for_each(el, &timers)
    {
      polyfill_timer_t *tmr = list_entry(el, polyfill_timer_t, link);
      xTimerHandle timer = tmr->timer;
      if (xTimerIsTimerActive(timer)) {
        goto poll;
      }
    }

    break;
  }

  return 1;
}
