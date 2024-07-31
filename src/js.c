#include "js.h"
#include "polyfills.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

static JSValue __logger_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, const char *color)
{
  puts(color);

  for (int i = 0; i < argc; i++) {
    // add space between arguments
    if (i != 0) {
      puts(" ");
    }

    // convert argument to C string
    size_t len;
    const char *str = NULL;

    if (JS_IsObject(argv[i])) {
      JSValue stringified = JS_JSONStringify(ctx, argv[i], JS_UNDEFINED, JS_UNDEFINED);
      str = JS_ToCStringLen(ctx, &len, stringified);
      JS_FreeValue(ctx, stringified);
    } else if (JS_IsString(argv[i])) {
      str = JS_ToCStringLen(ctx, &len, argv[i]);
    }

    if (!str) {
      puts("(null)");
      continue;
    }

    puts(str);

    JS_FreeCString(ctx, str);
  }

  puts("\n");

  return JS_UNDEFINED;
}

static JSValue __console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\x1B[0m");
}

static JSValue __console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\x1B[33m");
}

static JSValue __console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\x1B[31m");
}

int isere_js_init(isere_js_t *js)
{
  if (js->runtime != NULL || js->context != NULL) {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "QuickJS runtime and context already initialized");
    return -1;
  }

  // initialize quickjs runtime
  js->runtime = JS_NewRuntime();
  if (js->runtime == NULL)
  {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS runtime");
    return -1;
  }
  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  JS_SetMaxStackSize(js->runtime, ISERE_JS_STACK_SIZE);

  // initialize quickjs context
  js->context = JS_NewContextRaw(js->runtime);
  if (js->context == NULL)
  {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS context");
    return -1;
  }

  // attach isere_js_t object to QuickJS context
  JS_SetContextOpaque(js->context, js);

  JS_AddIntrinsicBaseObjects(js->context);
  JS_AddIntrinsicDate(js->context);
  JS_AddIntrinsicEval(js->context);
  JS_AddIntrinsicStringNormalize(js->context);
  JS_AddIntrinsicRegExp(js->context);
  JS_AddIntrinsicJSON(js->context);
  JS_AddIntrinsicProxy(js->context);
  JS_AddIntrinsicMapSet(js->context);
  JS_AddIntrinsicTypedArrays(js->context);
  JS_AddIntrinsicPromise(js->context);
  JS_AddIntrinsicBigInt(js->context);

  JSValue global_obj = JS_GetGlobalObject(js->context);
  // add console.log(), console.warn(), and console.error() function
  JSValue console = JS_NewObject(js->context);
  JS_SetPropertyStr(js->context, console, "log", JS_NewCFunction(js->context, __console_log, "log", 1));
  JS_SetPropertyStr(js->context, console, "warn", JS_NewCFunction(js->context, __console_warn, "warn", 1));
  JS_SetPropertyStr(js->context, console, "error", JS_NewCFunction(js->context, __console_error, "error", 1));
  JS_SetPropertyStr(js->context, global_obj, "console", console);

  // TODO: environment variables
  // add process.env
  JSValue process = JS_NewObject(js->context);
  JSValue env = JS_NewObject(js->context);
  JS_SetPropertyStr(js->context, env, "NODE_ENV", JS_NewString(js->context, "production"));
  JS_SetPropertyStr(js->context, process, "env", env);
  JS_SetPropertyStr(js->context, global_obj, "process", process);

  // add setTimeout / clearTimeout
  isere_js_polyfill_timer_init(js);
  // isere_js_polyfill_fetch_init(js->context);

  JS_FreeValue(js->context, global_obj);

  return 0;
}

int isere_js_deinit(isere_js_t *js)
{
  if (js->context) {
    isere_js_polyfill_timer_deinit(js);
    // isere_js_polyfill_fetch_deinit(js->context);

    JS_FreeContext(js->context);
  }

  if (js->runtime) {
    JS_FreeRuntime(js->runtime);
  }

  return 0;
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

int isere_js_eval(isere_js_t *js, unsigned char *handler, unsigned int handler_len)
{
  JSValue global_obj = JS_GetGlobalObject(js->context);

  // add callback for getting the result
  JS_SetPropertyStr(js->context, global_obj, "cb", JS_NewCFunction(js->context, __handler_cb, "cb", 1));
  JS_FreeValue(js->context, global_obj);

  JSValue h = JS_Eval(js->context, (const char *)handler, handler_len, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(h)) {
    // TODO: error goes to logger
    js_std_dump_error(js->context);
    JS_FreeValue(js->context, h);
    return -1;
  }

  js_module_set_import_meta(js->context, h, 0, 0);
  JS_FreeValue(js->context, h);

  const char *eval =
    "import { handler } from 'handler';"
    "const handler1 = new Promise(resolve => handler(__event, __context, resolve).then(resolve));"
    "Promise.resolve(handler1).then(cb)";

  js->future = JS_Eval(js->context, eval, strlen(eval), "<isere>", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_BACKTRACE_BARRIER);
  if (JS_IsException(js->future)) {
    // TODO: error goes to logger
    js_std_dump_error(js->context);
    return -1;
  }

  return 0;
}

int isere_js_poll(isere_js_t *js)
{
  JSContext *ctx1;
  int err;
  int tmrerr = 0;

  // execute the pending timers
  tmrerr = isere_js_polyfill_timer_poll(js);

  // execute the pending jobs
  err = JS_ExecutePendingJob(JS_GetRuntime(js->context), &ctx1);
  if (err < 0) {
    js_std_dump_error(ctx1);
  }

  if (tmrerr == 0 && err <= 0) {
    return 0;
  }

  return 1;
}
