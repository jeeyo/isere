#ifndef ISERE_POLYFILLS_H
#define ISERE_POLYFILLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "FreeRTOS.h"
#include "timers.h"

#include "quickjs.h"

#define ISERE_POLYFILLS_MAX_TIMERS 10

extern JSClassID polyfill_timer_class_id;

typedef struct {
  TimerHandle_t timer;
  JSContext *ctx;
  JSValue func;
} polyfill_timer_t;

void polyfill_timer_init(JSContext *ctx);
void polyfill_timer_deinit(JSContext *ctx);
int polyfill_timer_poll(JSContext *ctx);

void polyfill_fetch_init(JSContext *ctx);
void polyfill_fetch_deinit(JSContext *ctx);
int polyfill_fetch_poll(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_POLYFILLS_H */
