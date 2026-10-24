// QuickJS (quickjs-ng) binding: the natives behind src/prelude.js, module loading
// from the app bundle, and the instance lifecycle used by src/engine.c.

#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"

#include "app.h"
#include "crypto.h"
#include "fetch.h"
#include "mem.h"
#include "platform.h"

#ifndef ISERE_JS_STACK_SIZE
#define ISERE_JS_STACK_SIZE (96 * 1024)
#endif

struct js_vm {
  JSRuntime *rt;
  JSContext *ctx;
  JSValue dispatch;
  size_t *resident;     // per app module, measured while loading it
};

typedef struct {
  JSValue value;
} qjs_ref_t;

// ISERE_JS_BYTECODE=1: compile the prelude and every module once, then load
// instances from bytecode instead of parsing source (criterion 10)
static int __bytecode = -1;
static uint8_t *__prelude_bc = NULL;
static size_t __prelude_bc_len = 0;
static uint8_t **__module_bc = NULL;
static size_t *__module_bc_len = NULL;
static uint8_t **__part_bc = NULL;
static size_t *__part_bc_len = NULL;

// ------------------------------------------------------------------ memory

static void *__js_calloc(void *opaque, size_t count, size_t size)
{
  return isere_calloc(count, size);
}

static void *__js_malloc(void *opaque, size_t size)
{
  return isere_malloc(size);
}

static void __js_free(void *opaque, void *ptr)
{
  isere_free(ptr);
}

static void *__js_realloc(void *opaque, void *ptr, size_t size)
{
  return isere_realloc(ptr, size);
}

static size_t __js_usable_size(const void *ptr)
{
  return isere_malloc_usable_size(ptr);
}

static const JSMallocFunctions __mf = {
  __js_calloc,
  __js_malloc,
  __js_free,
  __js_realloc,
  __js_usable_size,
};

// ------------------------------------------------------------------ errors

static void __dump_exception(JSContext *ctx, const char *where)
{
  JSValue exc = JS_GetException(ctx);
  const char *msg = JS_ToCString(ctx, exc);
  JSValue stack = JS_IsError(exc) ? JS_GetPropertyStr(ctx, exc, "stack") : JS_UNDEFINED;
  const char *st = JS_IsString(stack) ? JS_ToCString(ctx, stack) : NULL;
  fprintf(stderr, "[js:error] %s: %s\n%s", where, msg != NULL ? msg : "(unknown)", st != NULL ? st : "");
  fflush(stderr);
  if (st != NULL) JS_FreeCString(ctx, st);
  if (msg != NULL) JS_FreeCString(ctx, msg);
  JS_FreeValue(ctx, stack);
  JS_FreeValue(ctx, exc);
}

static int __interrupt(JSRuntime *rt, void *opaque)
{
  return isere_js_should_interrupt();
}

// ------------------------------------------------------------------ modules

static int __find_module(const char *name)
{
  for (uint32_t i = 0; i < isere_app.num_modules; i++) {
    if (strcmp(isere_app.modules[i].name, name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static char *__normalize(JSContext *ctx, const char *base, const char *name, void *opaque)
{
  // app modules live in one flat namespace: "./jwt.js" and "jwt.js" are the same module
  while (name[0] == '.' && name[1] == '/') {
    name += 2;
  }
  return js_strdup(ctx, name);
}

static JSValue __compile_module(JSContext *ctx, int index)
{
  const isere_app_module_t *m = &isere_app.modules[index];

  if (__bytecode && __module_bc[index] != NULL) {
    return JS_ReadObject(ctx, __module_bc[index], __module_bc_len[index], JS_READ_OBJ_BYTECODE);
  }

  JSValue fn = JS_Eval(ctx, m->source, m->source_len, m->name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (__bytecode && !JS_IsException(fn)) {
    size_t len = 0;
    uint8_t *bc = JS_WriteObject(ctx, &len, fn, JS_WRITE_OBJ_BYTECODE);
    if (bc != NULL) {
      // keep the bytecode outside any runtime, so it outlives this instance
      __module_bc[index] = isere_malloc(len);
      if (__module_bc[index] != NULL) {
        memcpy(__module_bc[index], bc, len);
        __module_bc_len[index] = len;
      }
      js_free(ctx, bc);
    }
  }
  return fn;
}

static JSModuleDef *__load_module(JSContext *ctx, const char *name, void *opaque)
{
  int index = __find_module(name);
  if (index < 0) {
    JS_ThrowReferenceError(ctx, "module '%s' is not in the app", name);
    return NULL;
  }

  JSValue fn = __compile_module(ctx, index);
  if (JS_IsException(fn)) {
    return NULL;
  }
  JSModuleDef *def = JS_VALUE_GET_PTR(fn);
  JS_FreeValue(ctx, fn);
  return def;
}

// ------------------------------------------------------------------ natives

static js_vm_t *__vm(JSContext *ctx)
{
  return (js_vm_t *)JS_GetContextOpaque(ctx);
}

static js_ref_t __ref(JSValueConst v, JSContext *ctx)
{
  qjs_ref_t *ref = isere_malloc(sizeof(qjs_ref_t));
  if (ref != NULL) {
    ref->value = JS_DupValue(ctx, v);
  }
  return ref;
}

static JSValue __n_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  int32_t level = 0;
  JS_ToInt32(ctx, &level, argv[0]);
  size_t len = 0;
  const char *msg = JS_ToCStringLen(ctx, &len, argv[1]);
  if (msg != NULL) {
    isere_js_log(level, msg, len);
    JS_FreeCString(ctx, msg);
  }
  return JS_UNDEFINED;
}

static JSValue __n_set_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  double delay = 0;
  if (!JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "callback is not a function");
  }
  JS_ToFloat64(ctx, &delay, argv[1]);
  js_ref_t fn = __ref(argv[0], ctx);
  if (fn == NULL) {
    return JS_ThrowOutOfMemory(ctx);
  }
  uint32_t id = isere_js_set_timer(__vm(ctx), fn, delay > 0 ? (uint64_t)delay : 0, JS_ToBool(ctx, argv[2]));
  if (id == 0) {
    return JS_ThrowInternalError(ctx, "could not create timer");
  }
  return JS_NewUint32(ctx, id);
}

static JSValue __n_clear_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  uint32_t id = 0;
  if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
    isere_js_clear_timer(__vm(ctx), id);
  }
  return JS_UNDEFINED;
}

static JSValue __n_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  return JS_NewFloat64(ctx, (double)platform_now_us() / 1000.0);
}

static JSValue __n_utf8_encode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  size_t len = 0;
  const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
  if (s == NULL) {
    return JS_EXCEPTION;
  }
  JSValue out = JS_NewUint8ArrayCopy(ctx, (const uint8_t *)s, len);
  JS_FreeCString(ctx, s);
  return out;
}

static JSValue __n_utf8_decode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  size_t len = 0;
  uint8_t *p = JS_GetUint8Array(ctx, &len, argv[0]);
  if (p == NULL) {
    return JS_EXCEPTION;
  }
  return JS_NewStringLen(ctx, (const char *)p, len);
}

static JSValue __n_base64(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int decode)
{
  size_t len = 0;
  const char *in = JS_ToCStringLen(ctx, &len, argv[0]);
  if (in == NULL) {
    return JS_EXCEPTION;
  }
  size_t cap = decode ? len * 2 + 4 : len * 2 + 8;
  char *out = isere_malloc(cap);
  if (out == NULL) {
    JS_FreeCString(ctx, in);
    return JS_ThrowOutOfMemory(ctx);
  }
  int n = decode ? isere_atob_utf8(in, len, out, cap) : isere_btoa_utf8(in, len, out, cap);
  JS_FreeCString(ctx, in);
  JSValue r = n < 0 ? JS_ThrowTypeError(ctx, "InvalidCharacterError") : JS_NewStringLen(ctx, out, (size_t)n);
  isere_free(out);
  return r;
}

static JSValue __n_atob(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  return __n_base64(ctx, this_val, argc, argv, 1);
}

static JSValue __n_btoa(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  return __n_base64(ctx, this_val, argc, argv, 0);
}

// compiles a prelude part (src/prelude/<name>.js) and returns its factory function
static JSValue __n_part(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  size_t len = 0;
  const char *name = JS_ToCStringLen(ctx, &len, argv[0]);
  if (name == NULL) {
    return JS_EXCEPTION;
  }
  const isere_app_module_t *part = isere_js_prelude_part(name, len);
  JS_FreeCString(ctx, name);
  if (part == NULL) {
    return JS_ThrowReferenceError(ctx, "no such prelude part");
  }
  size_t index = (size_t)(part - isere_app.prelude_parts);

  JSValue fn;
  if (__bytecode && __part_bc[index] != NULL) {
    fn = JS_ReadObject(ctx, __part_bc[index], __part_bc_len[index], JS_READ_OBJ_BYTECODE);
  } else {
    fn = JS_Eval(ctx, part->source, part->source_len, part->name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (__bytecode && !JS_IsException(fn)) {
      size_t bc_len = 0;
      uint8_t *bc = JS_WriteObject(ctx, &bc_len, fn, JS_WRITE_OBJ_BYTECODE);
      if (bc != NULL) {
        __part_bc[index] = isere_malloc(bc_len);
        if (__part_bc[index] != NULL) {
          memcpy(__part_bc[index], bc, bc_len);
          __part_bc_len[index] = bc_len;
        }
        js_free(ctx, bc);
      }
    }
  }
  if (JS_IsException(fn)) {
    return fn;
  }
  return JS_EvalFunction(ctx, fn);
}

static JSValue __n_respond(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  uint32_t id = 0;
  int32_t status = 500;
  JS_ToUint32(ctx, &id, argv[0]);
  JS_ToInt32(ctx, &status, argv[1]);

  isere_request_t *req = isere_js_find_request(id);
  if (req == NULL || req->responded) {
    // the request timed out or was already answered
    return JS_UNDEFINED;
  }

  isere_response_start(req, status);

  int64_t n = 0;
  JS_GetLength(ctx, argv[2], &n);
  for (int64_t i = 0; i + 1 < n; i += 2) {
    JSValue name = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
    JSValue value = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i + 1);
    size_t name_len = 0, value_len = 0;
    const char *name_s = JS_ToCStringLen(ctx, &name_len, name);
    const char *value_s = JS_ToCStringLen(ctx, &value_len, value);
    if (name_s != NULL && value_s != NULL) {
      isere_response_header(req, name_s, name_len, value_s, value_len);
    }
    if (name_s != NULL) JS_FreeCString(ctx, name_s);
    if (value_s != NULL) JS_FreeCString(ctx, value_s);
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, value);
  }

  if (JS_IsString(argv[3])) {
    size_t len = 0;
    const char *body = JS_ToCStringLen(ctx, &len, argv[3]);
    isere_response_finish(req, body, body != NULL ? len : 0);
    if (body != NULL) JS_FreeCString(ctx, body);
  } else if (JS_IsObject(argv[3])) {
    size_t len = 0;
    uint8_t *body = JS_GetUint8Array(ctx, &len, argv[3]);
    if (body == NULL) {
      JS_FreeValue(ctx, JS_GetException(ctx));
    }
    isere_response_finish(req, (const char *)body, body != NULL ? len : 0);
  } else {
    isere_response_finish(req, NULL, 0);
  }
  return JS_UNDEFINED;
}

static JSValue __n_verify(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  const char *kind = JS_ToCString(ctx, argv[0]);
  const char *hash = JS_ToCString(ctx, argv[1]);
  size_t a_len = 0, b_len = 0, sig_len = 0, data_len = 0;
  const char *a = JS_ToCStringLen(ctx, &a_len, argv[2]);
  const char *b = JS_ToCStringLen(ctx, &b_len, argv[3]);
  uint8_t *sig = JS_GetUint8Array(ctx, &sig_len, argv[4]);
  uint8_t *data = JS_GetUint8Array(ctx, &data_len, argv[5]);

  JSValue result;
  if (kind == NULL || hash == NULL || a == NULL || b == NULL || sig == NULL || data == NULL) {
    result = JS_EXCEPTION;
  } else {
    const char *error = NULL;
    int r = isere_crypto_verify(kind[0] == 'R' ? ISERE_KEY_RSA : ISERE_KEY_EC_P256, atoi(hash + 4),
      a, a_len, b, b_len, sig, sig_len, data, data_len, &error);
    result = r < 0 ? JS_ThrowTypeError(ctx, "%s", error != NULL ? error : "verify failed") : JS_NewBool(ctx, r == 1);
  }

  if (kind != NULL) JS_FreeCString(ctx, kind);
  if (hash != NULL) JS_FreeCString(ctx, hash);
  if (a != NULL) JS_FreeCString(ctx, a);
  if (b != NULL) JS_FreeCString(ctx, b);
  return result;
}

static JSValue __n_digest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  const char *hash = JS_ToCString(ctx, argv[0]);
  size_t len = 0;
  uint8_t *data = JS_GetUint8Array(ctx, &len, argv[1]);
  if (hash == NULL || data == NULL) {
    if (hash != NULL) JS_FreeCString(ctx, hash);
    return JS_EXCEPTION;
  }
  uint8_t out[64];
  int n = isere_crypto_digest(atoi(hash + 4), data, len, out);
  JS_FreeCString(ctx, hash);
  if (n < 0) {
    return JS_ThrowTypeError(ctx, "digest failed");
  }
  return JS_NewUint8ArrayCopy(ctx, out, (size_t)n);
}

static JSValue __n_random_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  size_t len = 0;
  uint8_t *p = JS_GetUint8Array(ctx, &len, argv[0]);
  if (p == NULL) {
    return JS_EXCEPTION;
  }
  if (len > 65536) {
    return JS_ThrowRangeError(ctx, "getRandomValues: more than 65536 bytes");
  }
  if (isere_crypto_random(p, len) < 0) {
    return JS_ThrowInternalError(ctx, "no randomness available");
  }
  return JS_UNDEFINED;
}

static JSValue __n_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  isere_fetch_request_t r;
  memset(&r, 0, sizeof(r));
  JSValue result = JS_UNDEFINED;

  if (!JS_IsFunction(ctx, argv[4])) {
    return JS_ThrowTypeError(ctx, "callback is not a function");
  }

  r.method = JS_ToCStringLen(ctx, &r.method_len, argv[0]);
  r.url = JS_ToCStringLen(ctx, &r.url_len, argv[1]);

  int64_t n = 0;
  JS_GetLength(ctx, argv[2], &n);
  size_t count = (size_t)(n / 2);
  const char **names = isere_calloc(count + 1, sizeof(char *));
  const char **values = isere_calloc(count + 1, sizeof(char *));
  size_t *name_lens = isere_calloc(count + 1, sizeof(size_t));
  size_t *value_lens = isere_calloc(count + 1, sizeof(size_t));

  if (r.method == NULL || r.url == NULL || names == NULL || values == NULL || name_lens == NULL || value_lens == NULL) {
    result = JS_ThrowOutOfMemory(ctx);
    goto done;
  }

  for (size_t i = 0; i < count; i++) {
    JSValue name = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)(2 * i));
    JSValue value = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)(2 * i + 1));
    names[i] = JS_ToCStringLen(ctx, &name_lens[i], name);
    values[i] = JS_ToCStringLen(ctx, &value_lens[i], value);
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, value);
  }
  r.header_names = names;
  r.header_name_lens = name_lens;
  r.header_values = values;
  r.header_value_lens = value_lens;
  r.num_headers = count;

  const char *body_s = NULL;
  if (JS_IsString(argv[3])) {
    body_s = JS_ToCStringLen(ctx, &r.body_len, argv[3]);
    r.body = body_s;
  } else if (JS_IsObject(argv[3])) {
    r.body = (const char *)JS_GetUint8Array(ctx, &r.body_len, argv[3]);
  }

  js_ref_t fn = __ref(argv[4], ctx);
  if (fn == NULL || isere_fetch_start(__vm(ctx), &r, fn) < 0) {
    result = JS_ThrowTypeError(ctx, "fetch could not start");
  }
  if (body_s != NULL) JS_FreeCString(ctx, body_s);

done:
  for (size_t i = 0; names != NULL && values != NULL && i < count; i++) {
    if (names[i] != NULL) JS_FreeCString(ctx, names[i]);
    if (values[i] != NULL) JS_FreeCString(ctx, values[i]);
  }
  isere_free(names);
  isere_free(values);
  isere_free(name_lens);
  isere_free(value_lens);
  if (r.method != NULL) JS_FreeCString(ctx, r.method);
  if (r.url != NULL) JS_FreeCString(ctx, r.url);
  return result;
}

static JSValue __n_spin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  double ms = 0;
  JS_ToFloat64(ctx, &ms, argv[0]);
  isere_js_spin(ms);
  return JS_UNDEFINED;
}

static JSValue __n_alloc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  double bytes = 0;
  JS_ToFloat64(ctx, &bytes, argv[0]);
  if (bytes > 0) {
    // through the engine's allocator, like any allocation a handler makes
    void *p = js_malloc(ctx, (size_t)bytes);
    if (p == NULL) {
      return JS_EXCEPTION;
    }
    memset(p, 1, (size_t)bytes);
    js_free(ctx, p);
  }
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry __natives[] = {
  JS_CFUNC_DEF("log", 2, __n_log),
  JS_CFUNC_DEF("setTimer", 3, __n_set_timer),
  JS_CFUNC_DEF("clearTimer", 1, __n_clear_timer),
  JS_CFUNC_DEF("now", 0, __n_now),
  JS_CFUNC_DEF("utf8Encode", 1, __n_utf8_encode),
  JS_CFUNC_DEF("utf8Decode", 1, __n_utf8_decode),
  JS_CFUNC_DEF("atob", 1, __n_atob),
  JS_CFUNC_DEF("part", 1, __n_part),
  JS_CFUNC_DEF("btoa", 1, __n_btoa),
  JS_CFUNC_DEF("respond", 4, __n_respond),
  JS_CFUNC_DEF("verify", 6, __n_verify),
  JS_CFUNC_DEF("digest", 2, __n_digest),
  JS_CFUNC_DEF("randomFill", 1, __n_random_fill),
  JS_CFUNC_DEF("fetch", 5, __n_fetch),
  JS_CFUNC_DEF("spin", 1, __n_spin),
  JS_CFUNC_DEF("alloc", 1, __n_alloc),
};

// ------------------------------------------------------------------ instance

size_t js_vm_heap_used(js_vm_t *vm);

static uint32_t __elapsed(uint64_t *since)
{
  uint64_t now = platform_now_us();
  uint32_t d = (uint32_t)(now - *since);
  *since = now;
  return d;
}

static int __drain_boot(js_vm_t *vm)
{
  JSContext *ctx1;
  int r;
  while ((r = JS_ExecutePendingJob(vm->rt, &ctx1)) != 0) {
    if (r < 0) {
      __dump_exception(ctx1, "job");
    }
  }
  return 0;
}

static int __eval_prelude(js_vm_t *vm)
{
  JSContext *ctx = vm->ctx;
  JSValue fn;
  if (__bytecode && __prelude_bc != NULL) {
    fn = JS_ReadObject(ctx, __prelude_bc, __prelude_bc_len, JS_READ_OBJ_BYTECODE);
  } else {
    fn = JS_Eval(ctx, isere_app.prelude, isere_app.prelude_len, "prelude.js",
      JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (__bytecode && !JS_IsException(fn)) {
      size_t len = 0;
      uint8_t *bc = JS_WriteObject(ctx, &len, fn, JS_WRITE_OBJ_BYTECODE);
      if (bc != NULL) {
        __prelude_bc = isere_malloc(len);
        if (__prelude_bc != NULL) {
          memcpy(__prelude_bc, bc, len);
          __prelude_bc_len = len;
        }
        js_free(ctx, bc);
      }
    }
  }
  if (JS_IsException(fn)) {
    __dump_exception(ctx, "prelude.js");
    return -1;
  }
  JSValue r = JS_EvalFunction(ctx, fn);
  if (JS_IsException(r)) {
    __dump_exception(ctx, "prelude.js");
    return -1;
  }
  JS_FreeValue(ctx, r);
  return 0;
}

static int __load_route_module(js_vm_t *vm, uint32_t route)
{
  JSContext *ctx = vm->ctx;
  const isere_app_route_t *r = &isere_app.routes[route];
  char src[160];
  int len = snprintf(src, sizeof(src), "import * as ns from '%s';\n__isere_register(%u, ns);\n",
    isere_app.modules[r->module].name, route);
  if (len <= 0 || (size_t)len >= sizeof(src)) {
    return -1;
  }

  JSValue p = JS_Eval(ctx, src, (size_t)len, "<isere>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(p)) {
    __dump_exception(ctx, isere_app.modules[r->module].name);
    return -1;
  }
  __drain_boot(vm);

  int ret = 0;
  if (JS_PromiseState(ctx, p) == JS_PROMISE_REJECTED) {
    JSValue reason = JS_PromiseResult(ctx, p);
    JS_Throw(ctx, reason);
    __dump_exception(ctx, isere_app.modules[r->module].name);
    ret = -1;
  }
  JS_FreeValue(ctx, p);
  return ret;
}

js_vm_t *js_vm_new(int module, uint32_t phase_us[ISERE_PHASE_COUNT])
{
  if (__bytecode < 0) {
    const char *bc = getenv("ISERE_JS_BYTECODE");
    __bytecode = bc != NULL && bc[0] == '1';
    if (__bytecode) {
      __module_bc = isere_calloc(isere_app.num_modules + 1, sizeof(uint8_t *));
      __module_bc_len = isere_calloc(isere_app.num_modules + 1, sizeof(size_t));
      __part_bc = isere_calloc(isere_app.num_prelude_parts + 1, sizeof(uint8_t *));
      __part_bc_len = isere_calloc(isere_app.num_prelude_parts + 1, sizeof(size_t));
      if (__module_bc == NULL || __module_bc_len == NULL || __part_bc == NULL || __part_bc_len == NULL) {
        __bytecode = 0;
      }
    }
    isere_crypto_init();
  }

  uint64_t t = platform_now_us();
  js_vm_t *vm = isere_calloc(1, sizeof(js_vm_t));
  if (vm == NULL) {
    return NULL;
  }
  vm->dispatch = JS_UNDEFINED;
  vm->resident = isere_calloc(isere_app.num_modules + 1, sizeof(size_t));

  vm->rt = JS_NewRuntime2(&__mf, NULL);
  if (vm->rt == NULL || vm->resident == NULL) {
    isere_free(vm->resident);
    isere_free(vm);
    return NULL;
  }
  JS_SetMaxStackSize(vm->rt, ISERE_JS_STACK_SIZE);
  JS_SetInterruptHandler(vm->rt, __interrupt, NULL);
  JS_SetModuleLoaderFunc(vm->rt, __normalize, __load_module, vm);
  phase_us[ISERE_PHASE_RUNTIME] += __elapsed(&t);

  vm->ctx = JS_NewContextRaw(vm->rt);
  if (vm->ctx == NULL) {
    js_vm_free(vm);
    return NULL;
  }
  JS_SetContextOpaque(vm->ctx, vm);
  JS_AddIntrinsicBaseObjects(vm->ctx);
  JS_AddIntrinsicDate(vm->ctx);
  JS_AddIntrinsicEval(vm->ctx);
  JS_AddIntrinsicRegExpCompiler(vm->ctx);
  JS_AddIntrinsicRegExp(vm->ctx);
  JS_AddIntrinsicJSON(vm->ctx);
  JS_AddIntrinsicMapSet(vm->ctx);
  JS_AddIntrinsicTypedArrays(vm->ctx);
  JS_AddIntrinsicPromise(vm->ctx);
  phase_us[ISERE_PHASE_CONTEXT] += __elapsed(&t);

  JSContext *ctx = vm->ctx;
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue natives = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, natives, __natives, sizeof(__natives) / sizeof(__natives[0]));
  size_t config_len = 0, overrides_len = 0;
  const char *config = isere_js_config(&config_len);
  const char *overrides = isere_js_config_overrides(&overrides_len);
  JS_SetPropertyStr(ctx, natives, "config", JS_NewStringLen(ctx, config, config_len));
  JS_SetPropertyStr(ctx, natives, "overrides", overrides != NULL ? JS_NewStringLen(ctx, overrides, overrides_len) : JS_UNDEFINED);
  JS_SetPropertyStr(ctx, natives, "lazy", JS_NewBool(ctx, isere_js_lazy()));
  JS_SetPropertyStr(ctx, global, "__isere", natives);

  int ok = __eval_prelude(vm) == 0;
  if (ok) {
    vm->dispatch = JS_GetPropertyStr(ctx, global, "__isere_dispatch");
    ok = JS_IsFunction(ctx, vm->dispatch);
  }
  JSAtom atom = JS_NewAtom(ctx, "__isere");
  JS_DeleteProperty(ctx, global, atom, 0);
  JS_FreeAtom(ctx, atom);
  atom = JS_NewAtom(ctx, "__isere_dispatch");
  JS_DeleteProperty(ctx, global, atom, 0);
  JS_FreeAtom(ctx, atom);
  phase_us[ISERE_PHASE_POLYFILLS] += __elapsed(&t);

  if (ok) {
    for (uint32_t i = 0; i < isere_app.num_routes; i++) {
      const isere_app_route_t *r = &isere_app.routes[i];
      if (r->kind != ISERE_ROUTE_JS || (module >= 0 && r->module != module)) {
        continue;
      }
      // what the engine holds after loading the module, garbage collected on both sides
      JS_RunGC(vm->rt);
      size_t before = js_vm_heap_used(vm);
      __load_route_module(vm, i);
      JS_RunGC(vm->rt);
      size_t after = js_vm_heap_used(vm);
      if (vm->resident[r->module] == 0 && after > before) {
        vm->resident[r->module] = after - before;
      }
    }
  }
  atom = JS_NewAtom(ctx, "__isere_register");
  JS_DeleteProperty(ctx, global, atom, 0);
  JS_FreeAtom(ctx, atom);
  JS_FreeValue(ctx, global);
  phase_us[ISERE_PHASE_MODULE] += __elapsed(&t);

  if (!ok) {
    js_vm_free(vm);
    return NULL;
  }
  return vm;
}

uint32_t js_vm_free(js_vm_t *vm)
{
  uint32_t leftovers = 0;
  if (vm->ctx != NULL) {
    JS_FreeValue(vm->ctx, vm->dispatch);
    JS_FreeContext(vm->ctx);
  }
  if (vm->rt != NULL) {
    // count what survives the context before JS_FreeRuntime() asserts on it
    JS_RunGC(vm->rt);
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(vm->rt, &usage);
    leftovers = (uint32_t)usage.obj_count;
    if (leftovers == 0) {
      JS_FreeRuntime(vm->rt);
    } else {
      // leaked on purpose: freeing it would abort the process
      fprintf(stderr, "[js:error] %u objects outlived their context; runtime leaked\n", leftovers);
    }
  }
  isere_free(vm->resident);
  isere_free(vm);
  return leftovers;
}

size_t js_vm_module_resident(js_vm_t *vm, int module)
{
  return module >= 0 ? vm->resident[module] : 0;
}

int js_vm_dead(js_vm_t *vm)
{
  // QuickJS reports running out of memory as an exception
  return 0;
}

void js_vm_enter(js_vm_t *vm)
{
  // the stack limit counts from where this stretch of JS work starts
  JS_UpdateStackTop(vm->rt);
}

void js_vm_leave(js_vm_t *vm)
{
}

int js_vm_dispatch(js_vm_t *vm, isere_request_t *req)
{
  JSContext *ctx = vm->ctx;
  const char *buf = req->buf;
  const http_request_head_t *head = req->head;

  // the one copy from the receive buffer into the engine, length-aware
  JSValue headers = JS_NewArray(ctx);
  for (size_t i = 0; i < head->num_headers; i++) {
    const http_header_t *h = &req->headers[i];
    JS_SetPropertyUint32(ctx, headers, (uint32_t)(2 * i), JS_NewStringLen(ctx, buf + h->name.off, h->name.len));
    JS_SetPropertyUint32(ctx, headers, (uint32_t)(2 * i + 1), JS_NewStringLen(ctx, buf + h->value.off, h->value.len));
  }

  JSValue args[7] = {
    JS_NewInt32(ctx, req->route),
    JS_NewUint32(ctx, req->id),
    JS_NewStringLen(ctx, buf + head->method.off, head->method.len),
    JS_NewStringLen(ctx, buf + head->path.off, head->path.len),
    JS_NewStringLen(ctx, buf + head->query.off, head->query.len),
    headers,
    JS_NewStringLen(ctx, buf + req->body.off, req->body.len),
  };

  JSValue r = JS_Call(ctx, vm->dispatch, JS_UNDEFINED, 7, args);
  for (int i = 0; i < 7; i++) {
    JS_FreeValue(ctx, args[i]);
  }
  if (JS_IsException(r)) {
    __dump_exception(ctx, "dispatch");
    return -1;
  }
  JS_FreeValue(ctx, r);
  return 0;
}

int js_vm_run_job(js_vm_t *vm)
{
  JSContext *ctx1;
  int r = JS_ExecutePendingJob(vm->rt, &ctx1);
  if (r < 0) {
    // an interrupted job: the error is reported and draining goes on
    __dump_exception(ctx1, "job");
    return 1;
  }
  return r;
}

int js_vm_call(js_vm_t *vm, js_ref_t fn)
{
  JSContext *ctx = vm->ctx;
  // the callback may clear its own timer, which releases `fn`
  JSValue f = JS_DupValue(ctx, ((qjs_ref_t *)fn)->value);
  JSValue r = JS_Call(ctx, f, JS_UNDEFINED, 0, NULL);
  JS_FreeValue(ctx, f);
  if (JS_IsException(r)) {
    __dump_exception(ctx, "timer");
    return -1;
  }
  JS_FreeValue(ctx, r);
  return 0;
}

void js_vm_release(js_vm_t *vm, js_ref_t fn)
{
  if (fn == NULL) {
    return;
  }
  qjs_ref_t *ref = (qjs_ref_t *)fn;
  JS_FreeValue(vm->ctx, ref->value);
  isere_free(ref);
}

void js_vm_gc(js_vm_t *vm)
{
  JS_RunGC(vm->rt);
}

size_t js_vm_heap_used(js_vm_t *vm)
{
  JSMemoryUsage usage;
  JS_ComputeMemoryUsage(vm->rt, &usage);
  return (size_t)usage.malloc_size;
}

void js_vm_fetch_done(js_vm_t *vm, js_ref_t fn, const isere_fetch_result_t *result)
{
  JSContext *ctx = vm->ctx;
  JSValue args[4];
  int argc;

  if (result->error != NULL) {
    args[0] = JS_NewString(ctx, result->error);
    argc = 1;
  } else {
    JSValue headers = JS_NewArray(ctx);
    for (size_t i = 0; i < result->num_headers; i++) {
      const http_header_t *h = &result->headers[i];
      JS_SetPropertyUint32(ctx, headers, (uint32_t)(2 * i), JS_NewStringLen(ctx, result->buf + h->name.off, h->name.len));
      JS_SetPropertyUint32(ctx, headers, (uint32_t)(2 * i + 1), JS_NewStringLen(ctx, result->buf + h->value.off, h->value.len));
    }
    args[0] = JS_UNDEFINED;
    args[1] = JS_NewInt32(ctx, result->status);
    args[2] = headers;
    args[3] = JS_NewUint8ArrayCopy(ctx, (const uint8_t *)result->body, result->body_len);
    argc = 4;
  }

  JSValue f = JS_DupValue(ctx, ((qjs_ref_t *)fn)->value);
  JSValue r = JS_Call(ctx, f, JS_UNDEFINED, argc, args);
  JS_FreeValue(ctx, f);
  for (int i = 0; i < argc; i++) {
    JS_FreeValue(ctx, args[i]);
  }
  if (JS_IsException(r)) {
    __dump_exception(ctx, "fetch callback");
  }
  JS_FreeValue(ctx, r);
}
