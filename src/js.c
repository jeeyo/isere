#include "js.h"
#include "polyfills.h"
#include "pvPortRealloc.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

static isere_t *__isere = NULL;

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

  puts("\033[0m\n");

  return JS_UNDEFINED;
}

static JSValue __console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\033[0m");
}

static JSValue __console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\033[0;33m");
}

static JSValue __console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return __logger_internal(ctx, this_val, argc, argv, "\033[0;31m");
}

#define MALLOC_OVERHEAD 0

static size_t js_def_malloc_usable_size(const void *ptr)
{
  return 0;
}

static void *js_def_malloc(JSMallocState *s, size_t size)
{
  void *ptr;

  // /* Do not allocate zero bytes: behavior is platform dependent */
  // assert(size != 0);
  if (size == 0) {
    return NULL;
  }

  // TODO: unlikely
  if (s->malloc_size + size > s->malloc_limit) {
    return NULL;
  }

  ptr = pvPortMalloc(size);
  if (!ptr) {
    return NULL;
  }

  s->malloc_count++;
  s->malloc_size += js_def_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
  return ptr;
}

static void js_def_free(JSMallocState *s, void *ptr)
{
  if (!ptr)
    return;

  s->malloc_count--;
  s->malloc_size -= js_def_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
  vPortFree(ptr);
}

static void *js_def_realloc(JSMallocState *s, void *ptr, size_t size)
{
    size_t old_size;

    if (!ptr) {
      if (size == 0)
        return NULL;
      return js_def_malloc(s, size);
    }
    old_size = js_def_malloc_usable_size(ptr);
    if (size == 0) {
      s->malloc_count--;
      s->malloc_size -= old_size + MALLOC_OVERHEAD;
      vPortFree(ptr);
      return NULL;
    }
    if (s->malloc_size + size - old_size > s->malloc_limit) {
      return NULL;
    }

    ptr = pvPortRealloc(ptr, size);
    if (!ptr) {
      return NULL;
    }

    s->malloc_size += js_def_malloc_usable_size(ptr) - old_size;
    return ptr;
}

static const JSMallocFunctions __mf = {
  js_def_malloc,
  js_def_free,
  js_def_realloc,
  NULL,
};

int isere_js_init(isere_t *isere, isere_js_t *js)
{
  __isere = isere;

  return 0;
}

int isere_js_deinit(isere_js_t *js)
{
  return 0;
}

int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx)
{
  if (ctx->runtime != NULL || ctx->context != NULL) {
    __isere->logger->error(ISERE_JS_LOG_TAG, "QuickJS runtime or context already initialized");
    return -1;
  }

  // initialize quickjs runtime
  // ctx->runtime = JS_NewRuntime();
  ctx->runtime = JS_NewRuntime2(&__mf, NULL);
  if (ctx->runtime == NULL)
  {
    __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS runtime");
    return -1;
  }

  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  // JS_SetMaxStackSize(js->runtime, ISERE_JS_STACK_SIZE);

  // initialize quickjs context
  ctx->context = JS_NewContextRaw(ctx->runtime);
  if (ctx->context == NULL)
  {
    __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS context");
    return -1;
  }

  // attach isere_js_context_t object to QuickJS context
  JS_SetContextOpaque(ctx->context, ctx);

  JS_AddIntrinsicBaseObjects(ctx->context);
  JS_AddIntrinsicDate(ctx->context);
  JS_AddIntrinsicEval(ctx->context);
  JS_AddIntrinsicJSON(ctx->context);
  JS_AddIntrinsicPromise(ctx->context);

  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  // add console.log(), console.warn(), and console.error() function
  JSValue console = JS_NewObject(ctx->context);
  JS_SetPropertyStr(ctx->context, console, "log", JS_NewCFunction(ctx->context, __console_log, "log", 1));
  JS_SetPropertyStr(ctx->context, console, "warn", JS_NewCFunction(ctx->context, __console_warn, "warn", 1));
  JS_SetPropertyStr(ctx->context, console, "error", JS_NewCFunction(ctx->context, __console_error, "error", 1));
  JS_SetPropertyStr(ctx->context, global_obj, "console", console);

  // TODO: environment variables
  // add process.env
  JSValue process = JS_NewObject(ctx->context);
  JSValue env = JS_NewObject(ctx->context);
  JS_SetPropertyStr(ctx->context, env, "NODE_ENV", JS_NewString(ctx->context, "production"));
  JS_SetPropertyStr(ctx->context, process, "env", env);
  JS_SetPropertyStr(ctx->context, global_obj, "process", process);

  // add setTimeout / clearTimeout
  isere_js_polyfill_timer_init(ctx);
  // isere_js_polyfill_fetch_init(ctx->context);

  JS_FreeValue(ctx->context, global_obj);

  return 0;
}

int isere_js_free_context(isere_js_context_t *ctx)
{
  if (ctx->context) {
    JS_FreeValue(ctx->context, ctx->future);

    isere_js_polyfill_timer_deinit(ctx);
    // isere_js_polyfill_fetch_deinit(ctx->context);

    JS_FreeContext(ctx->context);
  }

  if (ctx->runtime) {
    JS_FreeRuntime(ctx->runtime);
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
    "const handler1 = new Promise(resolve => handler(__event, __context, resolve).then(resolve));"
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
  }

  if (tmrerr == 0 && err <= 0) {
    return 0;
  }

  return 1;
}
