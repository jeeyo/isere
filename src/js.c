#include "js.h"
#include "runtime.h"
#include "polyfills.h"

#include "quickjs.h"
#include "quickjs-libc.h"

#include <string.h>

static isere_t *__isere = NULL;

int isere_js_init(isere_t *isere, isere_js_t *js)
{
  __isere = isere;
  return js_runtime_init(js);
}

int isere_js_deinit(isere_js_t *js)
{
  return js_runtime_deinit(js);
}

int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return js_runtime_init_context(js, ctx);
}

int isere_js_free_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return js_runtime_deinit_context(js, ctx);
}

/*
  Response object:
  ```
  {
    "isBase64Encoded": false, // Set to `true` for binary support.
    "statusCode": 200,
    "headers": {
        "header1Name": "header1Value",
        "header2Name": "header2Value",
    },
    "body": "...",
  }
  ```
*/

static JSValue __handler_cb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "not a function");
  }

  JSValueConst resp = argv[0];

  JSValue global_obj = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME, JS_DupValue(ctx, resp));
  JS_FreeValue(ctx, global_obj);

  return JS_UNDEFINED;
}

int isere_js_eval(isere_js_context_t *ctx, unsigned char *handler, unsigned int handler_len)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);

  // add callback for getting the result
  JS_SetPropertyStr(ctx->context, global_obj, "cb", JS_NewCFunction(ctx->context, __handler_cb, "cb", 1));
  JS_FreeValue(ctx->context, global_obj);

  JSValue h = JS_Eval(ctx->context, (const char *)handler, handler_len, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(h)) {
    // TODO: error goes to logger
    js_std_dump_error(ctx->context);
    JS_FreeValue(ctx->context, h);
    return -1;
  }

  js_module_set_import_meta(ctx->context, h, 0, 0);
  JS_FreeValue(ctx->context, h);

  const char *eval =
    "import { handler } from 'handler';"
    "const handler1 = new Promise(resolve => resolve(handler(globalThis.__event, globalThis.__context, resolve)));"
    "Promise.resolve(handler1).then(cb)";

  ctx->future = JS_Eval(ctx->context, eval, strlen(eval), "<isere>", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_BACKTRACE_BARRIER);
  if (JS_IsException(ctx->future)) {
    // TODO: error goes to logger
    js_std_dump_error(ctx->context);
    return -1;
  }

  return 0;
}

int isere_js_poll(isere_js_context_t *ctx)
{
  JSContext *ctx1;
  int err;
  int tmrerr = 0;

  // execute the pending timers
  tmrerr = isere_js_polyfill_timer_poll(ctx);

  // execute the pending jobs
  err = JS_ExecutePendingJob(JS_GetRuntime(ctx->context), &ctx1);
  if (err < 0) {
    js_std_dump_error(ctx1);
    return -1;
  }

  if (tmrerr == 0 && err <= 0) {
    return 0;
  }

  return 1;
}
