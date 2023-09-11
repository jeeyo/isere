#ifndef ISERE_POLYFILLS_H

#include "isere.h"

#include "FreeRTOS.h"
#include "timers.h"

#include "quickjs.h"
#include "klist.h"

#define ISERE_POLYFILLS_H

extern JSClassID polyfill_timer_class_id;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  struct list_head link;
  TimerHandle_t timer;
} polyfill_timer_t;

void polyfill_timer_init(JSContext *ctx);
void polyfill_timer_deinit(JSContext *ctx);
JSValue polyfill_timer_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue polyfill_timer_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
int polyfill_timer_poll(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_POLYFILLS_H */
