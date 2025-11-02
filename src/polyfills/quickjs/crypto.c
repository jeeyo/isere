#include "polyfills.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

#include "FreeRTOS.h"

JSClassID polyfill_crypto_class_id;

JSValue polyfill_crypto_getRandomValues(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
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

  JS_SetOpaque(obj, timer);
  return obj;
}

JSValue polyfill_crypto_randomUUID(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  // https://github.com/rkg82/uuid-v4/blob/main/uuid/v4/uuid.h
  unsigned char data[16] = {0};
  for (int index = 0; index < 16; ++index) {
    data[index] = (unsigned char)dist(engine);
  }
  data[6] = ((data[6] & 0x0f) | 0x40); // Version 4
  data[8] = ((data[8] & 0x3f) | 0x80); // Variant is 10
  return JS_NewString(ctx, data);
}

void isere_js_polyfill_crypto_init(isere_js_context_t *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);

  JSValue crypto = JS_NewObject(ctx->context);

  JS_SetPropertyStr(ctx->context, crypto, "getRandomValues", JS_NewCFunction(ctx->context, polyfill_crypto_getRandomValues, "getRandomValues", 1));
  JS_SetPropertyStr(ctx->context, crypto, "randomUUID", JS_NewCFunction(ctx->context, polyfill_crypto_randomUUID, "randomUUID", 0));

  JS_SetPropertyStr(ctx->context, global_obj, "crypto", crypto);
  JS_FreeValue(ctx->context, crypto);

  JS_FreeValue(ctx->context, global_obj);
}

void isere_js_polyfill_crypto_deinit(isere_js_context_t *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);

  JSAtom crypto = JS_NewAtom(ctx->context, "crypto");
  JS_DeleteProperty(ctx->context, global_obj, crypto, 0);
  JS_FreeAtom(ctx->context, crypto);

  JS_FreeValue(ctx->context, global_obj);
}
