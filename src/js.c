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
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->info);
}

static JSValue __console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->warning);
}

static JSValue __console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, __isere->logger->error);
}

int js_init(isere_t *isere, isere_js_t *js)
{
  __isere = isere;

  if (js->runtime != NULL || js->context != NULL) {
    __isere->logger->error("[%s] QuickJS runtime and context already initialized", ISERE_JS_LOG_TAG);
    return -1;
  }

  // initialize quickjs runtime
  js->runtime = JS_NewRuntime();
  if (!js->runtime)
  {
    __isere->logger->error("[%s] failed to create QuickJS runtime", ISERE_JS_LOG_TAG);
    return -1;
  }
  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  JS_SetMaxStackSize(js->runtime, ISERE_JS_STACK_SIZE);

  // initialize quickjs context
  js->context = JS_NewContextRaw(js->runtime);
  if (!js->context)
  {
    __isere->logger->error("[%s] failed to create QuickJS context", ISERE_JS_LOG_TAG);
    return -1;
  }
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

  // add process.env
  JSValue process = JS_NewObject(js->context);
  JSValue env = JS_NewObject(js->context);
  JS_SetPropertyStr(js->context, env, "NODE_ENV", JS_NewString(js->context, "production"));
  JS_SetPropertyStr(js->context, process, "env", env);
  JS_SetPropertyStr(js->context, global_obj, "process", process);

  JS_FreeValue(js->context, global_obj);

  return 0;
}

int js_deinit(isere_js_t *js)
{
  if (__isere) {
    __isere = NULL;
  }

  if (js->context) {
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
  if (JS_IsException(val)) {
    __isere->logger->error("[%s] failed to get response body", ISERE_JS_LOG_TAG);
    JS_FreeValue(ctx, val);
    return JS_EXCEPTION;
  }

  // if body is undefined, set it to empty string
  if (JS_IsUndefined(val)) {
    JS_SetPropertyStr(ctx, resp, BODY_PROP_NAME, JS_NewString(ctx, ""));
  }

  // TODO: set content-type to application/json if body is object
  if (JS_IsObject(val)) {
    //
  }

  // convert body to C string
  size_t len;
  const char *str = JS_ToCStringLen(ctx, &len, val);
  JS_FreeValue(ctx, val);
  if (!str) {
    __isere->logger->error("[%s] unable to convert body to native string", ISERE_JS_LOG_TAG);
    return JS_EXCEPTION;
  }

  __isere->logger->debug("handler response: %.*s\n", len, str);

  return JS_UNDEFINED;
}

int js_eval(isere_js_t *js)
{
  const char *eval = "import { handler } from 'handler';\n"
    "handler(event, context).then(cb);";

  JSValue val = JS_Eval(js->context, (const char *)__isere->loader->fn, __isere->loader->fn_size, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(val)) {
    // TODO: error goes to logger
    JS_FreeValue(js->context, val);
    return -1;
  }

  js_module_set_import_meta(js->context, val, 0, 0);

  // add callback for getting the result
  JSValue global_obj = JS_GetGlobalObject(js->context);
  JS_SetPropertyStr(js->context, global_obj, "cb", JS_NewCFunction(js->context, __handler_cb, "cb", 1));
  JS_FreeValue(js->context, global_obj);

  val = JS_Eval(js->context, eval, strlen(eval), "<cmdline>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(val)) {
    // TODO: error goes to logger
    js_std_dump_error(js->context);
    JS_FreeValue(js->context, val);
    return -1;
  }
  JS_FreeValue(js->context, val);
  
  js_std_loop(js->context);

  return 0;
}

char *js_last_error(isere_js_t *ctx)
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
