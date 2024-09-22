#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "pvPortRealloc.h"

#include "quickjs.h"
#include "quickjs-libc.h"

#include <string.h>
#include <stdint.h>

static inline int32_t MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
static inline int32_t MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }

static JSValue __handler_cb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue __logger_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, const char *color);
static JSValue __console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue __console_warn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue __console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

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

int js_runtime_init(isere_js_t *js)
{
  if (js->runtime != NULL) {
    return -1;
  }

  // initialize quickjs runtime
  // ctx->runtime = JS_NewRuntime();
  js->runtime = JS_NewRuntime2(&__mf, NULL);
  if (js->runtime == NULL)
  {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS runtime");
    return -1;
  }

  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  // JS_SetMaxStackSize(js->runtime, ISERE_JS_STACK_SIZE);
  return 0;
}

int js_runtime_deinit(isere_js_t *js)
{
  if (js->runtime == NULL) {
    return -1;
  }

  JS_FreeRuntime(js->runtime);
  return 0;
}

int js_runtime_eval_handler(
  isere_js_context_t *ctx,
  unsigned char *handler,
  unsigned int handler_len,
  const char *method,
  const char *path,
  const char *query,
  const char **request_header_names,
  const char **request_header_values,
  const uint32_t request_headers_len,
  const char *body)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);

  // TODO: `event` object: https://aws-lambda-for-python-developers.readthedocs.io/en/latest/02_event_and_context/
  JSValue event = JS_NewObject(ctx->context);
  JS_SetPropertyStr(ctx->context, event, "httpMethod", JS_NewString(ctx->context, method));
  JS_SetPropertyStr(ctx->context, event, "path", JS_NewString(ctx->context, path));

  // TODO: multi-value headers
  JSValue headers = JS_NewObject(ctx->context);
  {
    int i = request_headers_len;
    char *name = (char *)request_header_names;
    char *value = (char *)request_header_values;
    while (i) {
      JS_SetPropertyStr(ctx->context, headers, name, JS_NewString(ctx->context, value));
      while (*(++name) != '\0');
      while (*(++name) == '\0');
      while (*(++value) != '\0');
      while (*(++value) == '\0');
      i--;
    }
  }
  JS_SetPropertyStr(ctx->context, event, "headers", headers);

  // TODO: query string params
  // TODO: multi-value query string params
  JS_SetPropertyStr(ctx->context, event, "query", JS_NewString(ctx->context, query != NULL ? query : ""));

  // TODO: check `Content-Type: application/json`
  JSValue parsedBody = JS_ParseJSON(ctx->context, body, strlen(body), "<input>");
  if (!JS_IsObject(parsedBody)) {
    JS_FreeValue(ctx->context, parsedBody);
    parsedBody = JS_NewString(ctx->context, body);
  }
  JS_SetPropertyStr(ctx->context, event, "body", parsedBody);

  // TODO: binary body
  JS_SetPropertyStr(ctx->context, event, "isBase64Encoded", JS_FALSE);
  JS_SetPropertyStr(ctx->context, global_obj, "event", event);

  // `context` object
  JSValue context = JS_NewObject(ctx->context);
  // // TODO: a C function?
  // JS_SetPropertyStr(ctx->context, context, "getRemainingTimeInMillis", NULL);
  // TODO: make some of these dynamic and some correct
  JS_SetPropertyStr(ctx->context, context, "functionName", JS_NewString(ctx->context, "handler"));
  JS_SetPropertyStr(ctx->context, context, "functionVersion", JS_NewString(ctx->context, ISERE_APP_VERSION));
  JS_SetPropertyStr(ctx->context, context, "memoryLimitInMB", JS_NewInt32(ctx->context, 128));
  JS_SetPropertyStr(ctx->context, context, "logGroupName", JS_NewString(ctx->context, ISERE_APP_NAME));
  JS_SetPropertyStr(ctx->context, context, "logStreamName", JS_NewString(ctx->context, ISERE_APP_NAME));
  // TODO: callbackWaitsForEmptyEventLoop
  JS_SetPropertyStr(ctx->context, context, "callbackWaitsForEmptyEventLoop", JS_NewBool(ctx->context, 1));
  JS_SetPropertyStr(ctx->context, global_obj, "context", context);

  // callback for getting result
  JS_SetPropertyStr(ctx->context, global_obj, "cb", JS_NewCFunction(ctx->context, __handler_cb, "cb", 1));
  JS_FreeValue(ctx->context, global_obj);

  // import handler as a module
  JSValue h = JS_Eval(ctx->context, (const char *)handler, handler_len, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(h)) {
    js_std_dump_error(ctx->context);
    JS_FreeValue(ctx->context, h);
    return -1;
  }

  js_module_set_import_meta(ctx->context, h, 0, 0);
  JS_FreeValue(ctx->context, h);

  const char *eval =
    "import { handler } from 'handler';"
    "const handler1 = new Promise(resolve => resolve(handler(event, context, resolve)));"
    "Promise.resolve(handler1).then(cb)";

  ctx->future = JS_Eval(ctx->context, eval, strlen(eval), "<isere>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(ctx->future)) {
    js_std_dump_error(ctx->context);
    return -1;
  }

  return 0;
}

static JSValue __handler_cb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "response is not an object");
  }

  isere_js_context_t *context = JS_GetContextOpaque(ctx);
  httpd_response_object_t *response = &((httpd_conn_t *)context->opaque)->response;

  JSValueConst response_obj = argv[0];

  // HTTP response code
  JSValue statusCode = JS_GetPropertyStr(ctx, response_obj, "statusCode");
  if (!JS_IsNumber(statusCode)) {
    response->statusCode = 200;
  } else {
    if (JS_ToInt32(ctx, &response->statusCode, statusCode) < 0) {
      response->statusCode = 500;
    }
  }
  JS_FreeValue(ctx, statusCode);

  // HTTP response headers
  JSValue headers = JS_GetPropertyStr(ctx, response_obj, "headers");
  if (JS_IsObject(headers)) {

    JSPropertyEnum *props = NULL;
    uint32_t props_len = 0;

    if (JS_GetOwnPropertyNames(ctx, &props, &props_len, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

      response->num_header_fields = MIN(props_len, ISERE_HTTPD_MAX_HTTP_HEADERS);

      for (int i = 0; i < response->num_header_fields; i++) {

        const char *header_field_str = JS_AtomToCString(ctx, props[i].atom);

        JSValue header_value = JS_GetProperty(ctx, headers, props[i].atom);
        size_t header_value_len = 0;
        const char *header_value_str = JS_ToCStringLen(ctx, &header_value_len, header_value);

        response->header_names[i] = strdup(header_field_str);
        response->header_values[i] = strdup(header_value_str);

        JS_FreeCString(ctx, header_field_str);
        JS_FreeCString(ctx, header_value_str);
        JS_FreeValue(ctx, header_value);

        JS_FreeAtom(ctx, props[i].atom);
      }

      js_free(ctx, props);
    }
  }
  JS_FreeValue(ctx, headers);

  // send HTTP response body
  JSValue body = JS_GetPropertyStr(ctx, response_obj, "body");
  size_t body_len = 0;
  const char *body_str = NULL;

  if (JS_IsObject(body)) {
    JSValue stringifiedBody = JS_JSONStringify(ctx, body, JS_UNDEFINED, JS_UNDEFINED);
    body_str = JS_ToCStringLen(ctx, &body_len, stringifiedBody);
    JS_FreeValue(ctx, stringifiedBody);
  } else if (JS_IsString(body)) {
    body_str = JS_ToCStringLen(ctx, &body_len, body);
  }

  if (body_str != NULL) {
    response->body_len = body_len;
    response->body = strdup(body_str);
    JS_FreeCString(ctx, body_str);
  }
  JS_FreeValue(ctx, body);

  response->completed = 1;
  return JS_UNDEFINED;
}

int js_runtime_init_context(isere_js_t *js, isere_js_context_t *ctx)
{
  if (ctx->context != NULL) {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "QuickJS runtime or context already initialized");
    return -1;
  }

  // initialize quickjs context
  ctx->context = JS_NewContextRaw(js->runtime);
  if (ctx->context == NULL)
  {
    // __isere->logger->error(ISERE_JS_LOG_TAG, "failed to create QuickJS context");
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

int js_runtime_deinit_context(isere_js_t *js, isere_js_context_t *ctx)
{
  if (ctx->context == NULL) {
    return -1;
  }

  JS_FreeValue(ctx->context, ctx->future);

  isere_js_polyfill_timer_deinit(ctx);
  // isere_js_polyfill_fetch_deinit(ctx->context);

  JS_FreeContext(ctx->context);
  return 0;
}

int js_runtime_poll(isere_js_context_t *ctx)
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
