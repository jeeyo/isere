#include "js.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

static isere_t *__isere = NULL;

static JSValue __logger_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, void (*logger_fn)(const char *fmt, ...))
{
  if (logger_fn == NULL) {
    return JS_EXCEPTION;
  }

  for (int i = 0; i < argc; i++) {
    // add space between arguments
    if (i != 0) {
      logger_fn(" ");
    }

    // convert argument to C string
    size_t len;
    const char *str = JS_ToCStringLen(ctx, &len, argv[i]);
    if (!str) {
      return JS_EXCEPTION;
    }

    // print string using logger
    logger_fn(str);

    JS_FreeCString(ctx, str);
  }

  logger_fn("\n");

  return JS_UNDEFINED;
}

static JSValue __console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (__isere == NULL) return JS_EXCEPTION;
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->info);
}

static JSValue __console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (__isere == NULL) return JS_EXCEPTION;
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->warning);
}

static JSValue __console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (__isere == NULL) return JS_EXCEPTION;
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->error);
}

js_ctx_t *js_init(isere_t *isere)
{
  __isere = isere;

  // initialize quickjs runtime
  JSRuntime *rt = JS_NewRuntime();
  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  if (!rt)
  {
    isere->logger->error("failed to create QuickJS runtime");
    return NULL;
  }
  JS_SetMaxStackSize(rt, ISERE_JS_STACK_SIZE);

  // initialize quickjs context
  JSContext *ctx = JS_NewContextRaw(rt);
  if (!ctx)
  {
    isere->logger->error("failed to create QuickJS context");
    return NULL;
  }
  JS_AddIntrinsicBaseObjects(ctx);
  JS_AddIntrinsicDate(ctx);
  JS_AddIntrinsicEval(ctx);
  JS_AddIntrinsicStringNormalize(ctx);
  JS_AddIntrinsicRegExp(ctx);
  JS_AddIntrinsicJSON(ctx);
  JS_AddIntrinsicProxy(ctx);
  JS_AddIntrinsicMapSet(ctx);
  JS_AddIntrinsicTypedArrays(ctx);
  JS_AddIntrinsicPromise(ctx);
  JS_AddIntrinsicBigInt(ctx);

  JSValue global_obj = JS_GetGlobalObject(ctx);
  // add console.log(), console.warn(), and console.error() function
  JSValue console = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, __console_log, "log", 1));
  JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, __console_warn, "warn", 1));
  JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, __console_error, "error", 1));
  JS_SetPropertyStr(ctx, global_obj, "console", console);

  // add process.env
  JSValue process = JS_NewObject(ctx);
  JSValue env = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, env, "NODE_ENV", JS_NewString(ctx, "production"));
  JS_SetPropertyStr(ctx, process, "env", env);
  JS_SetPropertyStr(ctx, global_obj, "process", process);

  JS_FreeValue(ctx, global_obj);

  return (js_ctx_t *)ctx;
}

int js_deinit(js_ctx_t *ctx)
{
  if (__isere) {
    __isere = NULL;
  }

  JSRuntime *rt = NULL;
  if (ctx) {
    rt = JS_GetRuntime(ctx);
    JS_FreeContext(ctx);
  }

  if (rt) {
    JS_FreeRuntime(rt);
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

#define IS_BASE64_ENCODED_PROP_NAME "isBase64Encoded"
#define STATUS_CODE_PROP_NAME "statusCode"
#define HEADERS_PROP_NAME "headers"
#define BODY_PROP_NAME "body"

static JSValue __handler_cb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  if (__isere == NULL) {
    return JS_EXCEPTION;
  }

  if (argc < 1) {
    return JS_EXCEPTION;
  }

  JSValueConst resp = argv[0];

  // TODO: get headers

  // get body
  JSValue val = JS_GetPropertyStr(ctx, resp, BODY_PROP_NAME);
  if (JS_IsException(val))
  {
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
  }

  // if body is undefined, set it to empty string
  if (JS_IsUndefined(val)) {
    JS_SetPropertyStr(ctx, resp, BODY_PROP_NAME, JS_NewString(ctx, "test"));
  }

  // TODO: set content-type to application/json if body is object
  if (JS_IsObject(val)) {
  }

  // convert body to C string
  size_t len;
  const char *str = JS_ToCStringLen(ctx, &len, val);
  JS_FreeValue(ctx, val);
  if (!str) {
    return JS_EXCEPTION;
  }

  __isere->logger->debug("handler response: %.*s\n", len, str);

  return JS_UNDEFINED;
}

int js_eval(js_ctx_t *ctx, uint8_t *fn, uint32_t fn_len)
{
  JSContext *qjs = (JSContext *)ctx;

  const char *eval = "import { handler } from 'handler';\n"
    "handler().then(cb);";

  JSValue val = JS_Eval(qjs, (const char *)fn, fn_len, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(val))
  {
    JS_FreeValue(qjs, val);
    return -1;
  }

  js_module_set_import_meta(qjs, val, 0, 0);

  // add callback for getting the result
  JSValue global_obj = JS_GetGlobalObject(qjs);
  JS_SetPropertyStr(qjs, global_obj, "cb", JS_NewCFunction(qjs, __handler_cb, "cb", 1));
  JS_FreeValue(qjs, global_obj);

  val = JS_Eval(qjs, eval, strlen(eval), "<cmdline>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(val))
  {
    js_std_dump_error(qjs);
    JS_FreeValue(qjs, val);
    return -1;
  }
  JS_FreeValue(qjs, val);
  
  js_std_loop(qjs);

  return 0;
}

char *js_last_error(js_ctx_t *ctx)
{
  // // TODO: get last error from quickjs context
  // if (__qjs_context->rt->current_exception != JS_NULL) {
  //   JSValue exception_val = JS_GetException(__qjs_context);

  //   if (JS_IsError(__qjs_context, exception_val)) {
  //     const char *str = JS_ToCString(__qjs_context, exception_val);
  //     JS_FreeValue(__qjs_context, exception_val);

  //     if (str) {
  //       return strdup(str);
  //     }

  //     return "";
  //   }

  //   JS_FreeValue(__qjs_context, exception_val);
  // }
  return "";
}
