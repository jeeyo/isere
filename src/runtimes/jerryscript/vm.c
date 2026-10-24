// JerryScript binding: the natives behind src/prelude.js, module loading from the
// app bundle, and the instance lifecycle used by src/engine.c.
//
// Each instance is its own JerryScript context (JERRY_EXTERNAL_CONTEXT) with its
// own heap arena; js_vm_enter() makes it the current one.

#include "engine.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jerryscript.h"
#include "jerryscript-port.h"

#include "app.h"
#include "crypto.h"
#include "fetch.h"
#include "mem.h"
#include "platform.h"

// how many backward jumps between deadline checks
#define HALT_INTERVAL 256

struct js_vm {
  jerry_context_t *context;
  jerry_value_t dispatch;
  jerry_value_t *modules;   // per app module, once loaded
  size_t *resident;         // per app module, measured while loading it
  // JerryScript allocates inside its own arena: track it to account per request
  size_t arena_used;
  js_vm_t *prev;
  int dead;                 // hit a fatal error; its context can't be used again
};

typedef struct {
  jerry_value_t value;
} jerry_ref_t;

static jerry_context_t *__current = NULL;
static int __crypto_ready = 0;

// Running out of its heap is fatal to JerryScript. Every call into the engine is
// guarded: the fatal handler jumps back to the guard, which abandons the
// instance. All of a context's state lives in its own arena, so freeing the
// arena frees everything, without asking the broken engine to clean up.
static jmp_buf *__recover = NULL;

void jerry_port_fatal(jerry_fatal_code_t code)
{
  if (__recover != NULL) {
    longjmp(*__recover, code != 0 ? (int)code : 1);
  }
  fprintf(stderr, "[js:error] JerryScript fatal error %d\n", (int)code);
  exit((int)code);
}

#define GUARD(vm, call, on_fatal) \
  do { \
    jmp_buf __jb; \
    jmp_buf *__prev = __recover; \
    __recover = &__jb; \
    if (setjmp(__jb) == 0) { \
      call; \
      __recover = __prev; \
    } else { \
      __recover = __prev; \
      (vm)->dead = 1; \
      fprintf(stderr, "[js:error] JerryScript ran out of its %d KB heap; the instance is replaced\n", JERRY_GLOBAL_HEAP_SIZE); \
      on_fatal; \
    } \
  } while (0)

// ------------------------------------------------------------------ port

size_t jerry_port_context_alloc(size_t context_size)
{
  size_t total = context_size + JERRY_GLOBAL_HEAP_SIZE * 1024;
  __current = isere_malloc(total);
  return __current != NULL ? total : 0;
}

jerry_context_t *jerry_port_context_get(void)
{
  return __current;
}

void jerry_port_context_free(void)
{
  isere_free(__current);
  __current = NULL;
}

// ------------------------------------------------------------------ helpers

static size_t __heap_used(void)
{
  jerry_heap_stats_t stats;
  memset(&stats, 0, sizeof(stats));
  jerry_heap_stats(&stats);
  return stats.allocated_bytes;
}

// strings from the network may not be valid UTF-8, which JerryScript requires
static jerry_value_t __string(const char *s, size_t len)
{
  if (jerry_validate_string((const jerry_char_t *)s, (jerry_size_t)len, JERRY_ENCODING_UTF8)) {
    return jerry_string((const jerry_char_t *)s, (jerry_size_t)len, JERRY_ENCODING_UTF8);
  }
  // replace every byte of an invalid sequence with U+FFFD
  char *fixed = isere_malloc(len * 3 + 1);
  if (fixed == NULL) {
    return jerry_string_sz("");
  }
  size_t n = 0;
  for (size_t i = 0; i < len;) {
    unsigned char c = (unsigned char)s[i];
    size_t seq = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (seq > 0 && i + seq <= len &&
        jerry_validate_string((const jerry_char_t *)s + i, (jerry_size_t)seq, JERRY_ENCODING_UTF8)) {
      memcpy(fixed + n, s + i, seq);
      n += seq;
      i += seq;
    } else {
      memcpy(fixed + n, "\xEF\xBF\xBD", 3);
      n += 3;
      i++;
    }
  }
  jerry_value_t v = jerry_string((const jerry_char_t *)fixed, (jerry_size_t)n, JERRY_ENCODING_UTF8);
  isere_free(fixed);
  return v;
}

// a JS string as a NUL-terminated UTF-8 copy; free with isere_free()
static char *__cstring(jerry_value_t v, size_t *len)
{
  jerry_value_t str = jerry_value_is_string(v) ? jerry_value_copy(v) : jerry_value_to_string(v);
  if (jerry_value_is_exception(str)) {
    jerry_value_free(str);
    return NULL;
  }
  jerry_size_t size = jerry_string_size(str, JERRY_ENCODING_UTF8);
  char *buf = isere_malloc((size_t)size + 1);
  if (buf != NULL) {
    jerry_string_to_buffer(str, JERRY_ENCODING_UTF8, (jerry_char_t *)buf, size);
    buf[size] = '\0';
    if (len != NULL) *len = size;
  }
  jerry_value_free(str);
  return buf;
}

// the bytes of a Uint8Array, or NULL
static uint8_t *__bytes(jerry_value_t v, size_t *len)
{
  if (!jerry_value_is_typedarray(v)) {
    return NULL;
  }
  jerry_size_t offset = 0, length = 0;
  jerry_value_t buffer = jerry_typedarray_buffer(v, &offset, &length);
  uint8_t *data = jerry_value_is_exception(buffer) ? NULL : jerry_arraybuffer_data(buffer);
  jerry_value_free(buffer);
  *len = length;
  return data != NULL ? data + offset : (length == 0 ? (uint8_t *)"" : NULL);
}

static jerry_value_t __uint8array(const uint8_t *data, size_t len)
{
  jerry_value_t array = jerry_typedarray(JERRY_TYPEDARRAY_UINT8, (jerry_length_t)len);
  size_t n = 0;
  uint8_t *p = __bytes(array, &n);
  if (p != NULL && len > 0) {
    memcpy(p, data, len);
  }
  return array;
}

static void __print_exception(jerry_value_t v, const char *where)
{
  jerry_value_t err = jerry_exception_value(v, false);
  char *msg = __cstring(err, NULL);
  char *stack = NULL;
  if (jerry_value_is_object(err)) {
    jerry_value_t s = jerry_object_get_sz(err, "stack");
    if (jerry_value_is_array(s)) {
      jerry_value_t joined = jerry_value_to_string(s);
      stack = __cstring(joined, NULL);
      jerry_value_free(joined);
    }
    jerry_value_free(s);
  }
  fprintf(stderr, "[js:error] %s: %s\n%s%s", where, msg != NULL ? msg : "(unknown)",
    stack != NULL ? stack : "", stack != NULL ? "\n" : "");
  fflush(stderr);
  isere_free(msg);
  isere_free(stack);
  jerry_value_free(err);
}

static jerry_value_t __halt(void *user)
{
  if (isere_js_should_interrupt()) {
    return jerry_throw_abort(jerry_string_sz("interrupted: handler deadline exceeded"), true);
  }
  return jerry_undefined();
}

static uint32_t __arg_u32(const jerry_value_t args[], jerry_length_t argc, jerry_length_t i)
{
  return i < argc ? (uint32_t)jerry_value_as_number(args[i]) : 0;
}

static jerry_value_t __arg(const jerry_value_t args[], jerry_length_t argc, jerry_length_t i)
{
  return i < argc ? args[i] : jerry_undefined();
}

static js_vm_t *__vm(void);

// ------------------------------------------------------------------ natives

#define NATIVE(name) \
  static jerry_value_t name(const jerry_call_info_t *info, const jerry_value_t args[], const jerry_length_t argc)

NATIVE(__n_log)
{
  size_t len = 0;
  char *msg = __cstring(__arg(args, argc, 1), &len);
  if (msg != NULL) {
    isere_js_log((int)__arg_u32(args, argc, 0), msg, len);
    isere_free(msg);
  }
  return jerry_undefined();
}

NATIVE(__n_set_timer)
{
  if (argc < 1 || !jerry_value_is_function(args[0])) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "callback is not a function");
  }
  jerry_ref_t *ref = isere_malloc(sizeof(jerry_ref_t));
  if (ref == NULL) {
    return jerry_throw_sz(JERRY_ERROR_RANGE, "out of memory");
  }
  ref->value = jerry_value_copy(args[0]);
  double delay = argc > 1 ? jerry_value_as_number(args[1]) : 0;
  int repeat = argc > 2 && jerry_value_to_boolean(args[2]);
  uint32_t id = isere_js_set_timer(__vm(), ref, delay > 0 ? (uint64_t)delay : 0, repeat);
  if (id == 0) {
    return jerry_throw_sz(JERRY_ERROR_COMMON, "could not create timer");
  }
  return jerry_number(id);
}

NATIVE(__n_clear_timer)
{
  isere_js_clear_timer(__vm(), __arg_u32(args, argc, 0));
  return jerry_undefined();
}

NATIVE(__n_now)
{
  return jerry_number((double)platform_now_us() / 1000.0);
}

NATIVE(__n_utf8_encode)
{
  jerry_value_t str = jerry_value_to_string(__arg(args, argc, 0));
  if (jerry_value_is_exception(str)) {
    return str;
  }
  jerry_size_t size = jerry_string_size(str, JERRY_ENCODING_UTF8);
  jerry_value_t array = jerry_typedarray(JERRY_TYPEDARRAY_UINT8, size);
  size_t n = 0;
  uint8_t *p = __bytes(array, &n);
  if (p != NULL && size > 0) {
    jerry_string_to_buffer(str, JERRY_ENCODING_UTF8, p, size);
  }
  jerry_value_free(str);
  return array;
}

NATIVE(__n_utf8_decode)
{
  size_t len = 0;
  uint8_t *p = __bytes(__arg(args, argc, 0), &len);
  if (p == NULL) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "not a Uint8Array");
  }
  return __string((const char *)p, len);
}

NATIVE(__n_respond)
{
  isere_request_t *req = isere_js_find_request(__arg_u32(args, argc, 0));
  if (req == NULL || req->responded || argc < 4) {
    return jerry_undefined();
  }

  // bring the arena accounting up to date, so Server-Timing includes this stretch
  js_vm_t *vm = __vm();
  size_t used = __heap_used();
  isere_mem_account_external((int64_t)used - (int64_t)vm->arena_used);
  vm->arena_used = used;

  isere_response_start(req, (int)jerry_value_as_number(args[1]));

  jerry_length_t n = jerry_value_is_array(args[2]) ? jerry_array_length(args[2]) : 0;
  for (jerry_length_t i = 0; i + 1 < n; i += 2) {
    jerry_value_t name = jerry_object_get_index(args[2], i);
    jerry_value_t value = jerry_object_get_index(args[2], i + 1);
    size_t name_len = 0, value_len = 0;
    char *name_s = __cstring(name, &name_len);
    char *value_s = __cstring(value, &value_len);
    if (name_s != NULL && value_s != NULL) {
      isere_response_header(req, name_s, name_len, value_s, value_len);
    }
    isere_free(name_s);
    isere_free(value_s);
    jerry_value_free(name);
    jerry_value_free(value);
  }

  jerry_value_t body = args[3];
  if (jerry_value_is_string(body)) {
    // straight from the engine's string into the output buffer
    jerry_size_t size = jerry_string_size(body, JERRY_ENCODING_UTF8);
    char *p = isere_response_body(req, size);
    if (p != NULL && size > 0) {
      jerry_string_to_buffer(body, JERRY_ENCODING_UTF8, (jerry_char_t *)p, size);
    }
    isere_response_end(req);
  } else {
    size_t len = 0;
    uint8_t *p = __bytes(body, &len);
    isere_response_finish(req, (const char *)p, p != NULL ? len : 0);
  }
  return jerry_undefined();
}

NATIVE(__n_verify)
{
  if (argc < 6) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "verify: missing arguments");
  }
  char *kind = __cstring(args[0], NULL);
  char *hash = __cstring(args[1], NULL);
  size_t a_len = 0, b_len = 0, sig_len = 0, data_len = 0;
  char *a = __cstring(args[2], &a_len);
  char *b = __cstring(args[3], &b_len);
  uint8_t *sig = __bytes(args[4], &sig_len);
  uint8_t *data = __bytes(args[5], &data_len);

  jerry_value_t result;
  if (kind == NULL || hash == NULL || a == NULL || b == NULL || sig == NULL || data == NULL) {
    result = jerry_throw_sz(JERRY_ERROR_TYPE, "verify: bad arguments");
  } else {
    const char *error = NULL;
    int r = isere_crypto_verify(kind[0] == 'R' ? ISERE_KEY_RSA : ISERE_KEY_EC_P256, atoi(hash + 4),
      a, a_len, b, b_len, sig, sig_len, data, data_len, &error);
    result = r < 0 ? jerry_throw_sz(JERRY_ERROR_TYPE, error != NULL ? error : "verify failed") : jerry_boolean(r == 1);
  }
  isere_free(kind);
  isere_free(hash);
  isere_free(a);
  isere_free(b);
  return result;
}

NATIVE(__n_digest)
{
  char *hash = __cstring(__arg(args, argc, 0), NULL);
  size_t len = 0;
  uint8_t *data = __bytes(__arg(args, argc, 1), &len);
  if (hash == NULL || data == NULL) {
    isere_free(hash);
    return jerry_throw_sz(JERRY_ERROR_TYPE, "digest: bad arguments");
  }
  uint8_t out[64];
  int n = isere_crypto_digest(atoi(hash + 4), data, len, out);
  isere_free(hash);
  if (n < 0) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "digest failed");
  }
  return __uint8array(out, (size_t)n);
}

NATIVE(__n_random_fill)
{
  size_t len = 0;
  uint8_t *p = __bytes(__arg(args, argc, 0), &len);
  if (p == NULL) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "not a Uint8Array");
  }
  if (len > 65536) {
    return jerry_throw_sz(JERRY_ERROR_RANGE, "getRandomValues: more than 65536 bytes");
  }
  if (isere_crypto_random(p, len) < 0) {
    return jerry_throw_sz(JERRY_ERROR_COMMON, "no randomness available");
  }
  return jerry_undefined();
}

NATIVE(__n_fetch)
{
  if (argc < 5 || !jerry_value_is_function(args[4])) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, "callback is not a function");
  }

  isere_fetch_request_t r;
  memset(&r, 0, sizeof(r));
  char *method = __cstring(args[0], &r.method_len);
  char *url = __cstring(args[1], &r.url_len);
  r.method = method;
  r.url = url;

  jerry_length_t n = jerry_value_is_array(args[2]) ? jerry_array_length(args[2]) : 0;
  size_t count = n / 2;
  char **names = isere_calloc(count + 1, sizeof(char *));
  char **values = isere_calloc(count + 1, sizeof(char *));
  size_t *name_lens = isere_calloc(count + 1, sizeof(size_t));
  size_t *value_lens = isere_calloc(count + 1, sizeof(size_t));
  jerry_value_t result = jerry_undefined();
  char *body_s = NULL;

  if (method == NULL || url == NULL || names == NULL || values == NULL || name_lens == NULL || value_lens == NULL) {
    result = jerry_throw_sz(JERRY_ERROR_RANGE, "out of memory");
    goto done;
  }
  for (size_t i = 0; i < count; i++) {
    jerry_value_t name = jerry_object_get_index(args[2], (jerry_length_t)(2 * i));
    jerry_value_t value = jerry_object_get_index(args[2], (jerry_length_t)(2 * i + 1));
    names[i] = __cstring(name, &name_lens[i]);
    values[i] = __cstring(value, &value_lens[i]);
    jerry_value_free(name);
    jerry_value_free(value);
  }
  r.header_names = (const char *const *)names;
  r.header_name_lens = name_lens;
  r.header_values = (const char *const *)values;
  r.header_value_lens = value_lens;
  r.num_headers = count;

  if (jerry_value_is_string(args[3])) {
    body_s = __cstring(args[3], &r.body_len);
    r.body = body_s;
  } else if (jerry_value_is_typedarray(args[3])) {
    r.body = (const char *)__bytes(args[3], &r.body_len);
  }

  jerry_ref_t *ref = isere_malloc(sizeof(jerry_ref_t));
  if (ref == NULL) {
    result = jerry_throw_sz(JERRY_ERROR_RANGE, "out of memory");
    goto done;
  }
  ref->value = jerry_value_copy(args[4]);
  if (isere_fetch_start(__vm(), &r, ref) < 0) {
    result = jerry_throw_sz(JERRY_ERROR_TYPE, "fetch could not start");
  }

done:
  for (size_t i = 0; names != NULL && values != NULL && i < count; i++) {
    isere_free(names[i]);
    isere_free(values[i]);
  }
  isere_free(names);
  isere_free(values);
  isere_free(name_lens);
  isere_free(value_lens);
  isere_free(body_s);
  isere_free(method);
  isere_free(url);
  return result;
}

NATIVE(__n_spin)
{
  isere_js_spin(argc > 0 ? jerry_value_as_number(args[0]) : 0);
  return jerry_undefined();
}

NATIVE(__n_alloc)
{
  // inside the engine's own heap, like any allocation a handler makes
  double bytes = argc > 0 ? jerry_value_as_number(args[0]) : 0;
  if (bytes > 0) {
    jerry_value_t buffer = jerry_arraybuffer((jerry_length_t)bytes);
    if (jerry_value_is_exception(buffer)) {
      return buffer;
    }
    uint8_t *p = jerry_arraybuffer_data(buffer);
    if (p != NULL) memset(p, 1, (size_t)bytes);
    jerry_value_free(buffer);
  }
  return jerry_undefined();
}

static void __set_native(jerry_value_t obj, const char *name, jerry_external_handler_t fn)
{
  jerry_value_t f = jerry_function_external(fn);
  jerry_value_free(jerry_object_set_sz(obj, name, f));
  jerry_value_free(f);
}

// ------------------------------------------------------------------ modules

static js_vm_t *__current_vm = NULL;

static js_vm_t *__vm(void)
{
  return __current_vm;
}

static int __find_module(const char *name, size_t len)
{
  while (len >= 2 && name[0] == '.' && name[1] == '/') {
    name += 2;
    len -= 2;
  }
  for (uint32_t i = 0; i < isere_app.num_modules; i++) {
    const char *m = isere_app.modules[i].name;
    if (strncmp(m, name, len) == 0 && m[len] == '\0') {
      return (int)i;
    }
  }
  return -1;
}

static jerry_value_t __resolve(const jerry_value_t specifier, const jerry_value_t referrer, void *user)
{
  js_vm_t *vm = (js_vm_t *)user;
  size_t len = 0;
  char *name = __cstring(specifier, &len);
  int index = name != NULL ? __find_module(name, len) : -1;
  isere_free(name);
  if (index < 0) {
    return jerry_throw_sz(JERRY_ERROR_REFERENCE, "module is not in the app");
  }

  // one instance per module, however many modules import it
  if (vm->modules[index] == 0) {
    const isere_app_module_t *m = &isere_app.modules[index];
    jerry_parse_options_t opts;
    opts.options = JERRY_PARSE_MODULE | JERRY_PARSE_HAS_SOURCE_NAME;
    opts.source_name = jerry_string_sz(m->name);
    jerry_value_t module = jerry_parse((const jerry_char_t *)m->source, m->source_len, &opts);
    jerry_value_free(opts.source_name);
    if (jerry_value_is_exception(module)) {
      return module;
    }
    vm->modules[index] = module;
  }
  return jerry_value_copy(vm->modules[index]);
}

static int __load_route_module(js_vm_t *vm, uint32_t route)
{
  const isere_app_route_t *r = &isere_app.routes[route];
  char src[160];
  int len = snprintf(src, sizeof(src), "import * as ns from '%s';\n__isere_register(%u, ns);\n",
    isere_app.modules[r->module].name, route);
  if (len <= 0 || (size_t)len >= sizeof(src)) {
    return -1;
  }

  jerry_parse_options_t opts;
  opts.options = JERRY_PARSE_MODULE | JERRY_PARSE_HAS_SOURCE_NAME;
  opts.source_name = jerry_string_sz("<isere>");
  jerry_value_t module = jerry_parse((const jerry_char_t *)src, (size_t)len, &opts);
  jerry_value_free(opts.source_name);

  int ret = -1;
  if (jerry_value_is_exception(module)) {
    __print_exception(module, "<isere>");
    jerry_value_free(module);
    return -1;
  }

  jerry_value_t linked = jerry_module_link(module, __resolve, vm);
  if (jerry_value_is_exception(linked)) {
    __print_exception(linked, isere_app.modules[r->module].name);
  } else {
    jerry_value_t result = jerry_module_evaluate(module);
    if (jerry_value_is_exception(result)) {
      __print_exception(result, isere_app.modules[r->module].name);
    } else {
      ret = 0;
    }
    jerry_value_free(result);
  }
  jerry_value_free(linked);
  jerry_value_free(module);

  jerry_value_t jobs = jerry_run_jobs();
  if (jerry_value_is_exception(jobs)) {
    __print_exception(jobs, "job");
  }
  jerry_value_free(jobs);
  return ret;
}

// ------------------------------------------------------------------ instance

static uint32_t __elapsed(uint64_t *since)
{
  uint64_t now = platform_now_us();
  uint32_t d = (uint32_t)(now - *since);
  *since = now;
  return d;
}

void js_vm_enter(js_vm_t *vm)
{
  vm->prev = __current_vm;
  __current_vm = vm;
  __current = vm->context;
  if (!vm->dead) {
    vm->arena_used = __heap_used();
  }
}

void js_vm_leave(js_vm_t *vm)
{
  if (!vm->dead) {
    size_t used = __heap_used();
    isere_mem_account_external((int64_t)used - (int64_t)vm->arena_used);
    vm->arena_used = used;
  }
  __current_vm = vm->prev;
  __current = __current_vm != NULL ? __current_vm->context : NULL;
}

js_vm_t *js_vm_new(int module, uint32_t phase_us[ISERE_PHASE_COUNT])
{
  if (!__crypto_ready) {
    isere_crypto_init();
    __crypto_ready = 1;
  }

  uint64_t t = platform_now_us();
  js_vm_t *vm = isere_calloc(1, sizeof(js_vm_t));
  if (vm == NULL) {
    return NULL;
  }
  vm->modules = isere_calloc(isere_app.num_modules + 1, sizeof(jerry_value_t));
  vm->resident = isere_calloc(isere_app.num_modules + 1, sizeof(size_t));
  if (vm->modules == NULL || vm->resident == NULL) {
    isere_free(vm->modules);
    isere_free(vm->resident);
    isere_free(vm);
    return NULL;
  }

  // jerry_init() allocates the context through jerry_port_context_alloc()
  js_vm_t *outer = __current_vm;
  jerry_context_t *outer_context = __current;
  __current = NULL;
  jerry_init(JERRY_INIT_EMPTY);
  vm->context = __current;
  if (vm->context == NULL) {
    isere_free(vm->modules);
    isere_free(vm->resident);
    isere_free(vm);
    __current = outer_context;
    return NULL;
  }
  vm->prev = outer;
  __current_vm = vm;
  jerry_halt_handler(HALT_INTERVAL, __halt, NULL);
  phase_us[ISERE_PHASE_RUNTIME] += __elapsed(&t);

  // a JerryScript context comes with its realm and built-ins
  jerry_value_t global = jerry_current_realm();
  phase_us[ISERE_PHASE_CONTEXT] += __elapsed(&t);

  jerry_value_t natives = jerry_object();
  __set_native(natives, "log", __n_log);
  __set_native(natives, "setTimer", __n_set_timer);
  __set_native(natives, "clearTimer", __n_clear_timer);
  __set_native(natives, "now", __n_now);
  __set_native(natives, "utf8Encode", __n_utf8_encode);
  __set_native(natives, "utf8Decode", __n_utf8_decode);
  __set_native(natives, "respond", __n_respond);
  __set_native(natives, "verify", __n_verify);
  __set_native(natives, "digest", __n_digest);
  __set_native(natives, "randomFill", __n_random_fill);
  __set_native(natives, "fetch", __n_fetch);
  __set_native(natives, "spin", __n_spin);
  __set_native(natives, "alloc", __n_alloc);

  size_t config_len = 0, overrides_len = 0;
  const char *config = isere_js_config(&config_len);
  const char *overrides = isere_js_config_overrides(&overrides_len);
  jerry_value_t v = __string(config, config_len);
  jerry_value_free(jerry_object_set_sz(natives, "config", v));
  jerry_value_free(v);
  v = overrides != NULL ? __string(overrides, overrides_len) : jerry_undefined();
  jerry_value_free(jerry_object_set_sz(natives, "overrides", v));
  jerry_value_free(v);
  jerry_value_free(jerry_object_set_sz(global, "__isere", natives));
  jerry_value_free(natives);

  jerry_parse_options_t opts;
  opts.options = JERRY_PARSE_HAS_SOURCE_NAME;
  opts.source_name = jerry_string_sz("prelude.js");
  jerry_value_t script = jerry_parse((const jerry_char_t *)isere_app.prelude, isere_app.prelude_len, &opts);
  jerry_value_free(opts.source_name);
  int ok = 0;
  if (jerry_value_is_exception(script)) {
    __print_exception(script, "prelude.js");
  } else {
    jerry_value_t r = jerry_run(script);
    if (jerry_value_is_exception(r)) {
      __print_exception(r, "prelude.js");
    } else {
      vm->dispatch = jerry_object_get_sz(global, "__isere_dispatch");
      ok = jerry_value_is_function(vm->dispatch);
    }
    jerry_value_free(r);
  }
  jerry_value_free(script);
  jerry_value_free(jerry_object_delete_sz(global, "__isere"));
  jerry_value_free(jerry_object_delete_sz(global, "__isere_dispatch"));
  phase_us[ISERE_PHASE_POLYFILLS] += __elapsed(&t);

  if (ok) {
    for (uint32_t i = 0; i < isere_app.num_routes; i++) {
      const isere_app_route_t *r = &isere_app.routes[i];
      if (r->kind != ISERE_ROUTE_JS || (module >= 0 && r->module != module)) {
        continue;
      }
      size_t before = __heap_used();
      GUARD(vm, __load_route_module(vm, i), (void)0);
      if (vm->dead) {
        break;
      }
      size_t after = __heap_used();
      if (vm->resident[r->module] == 0 && after > before) {
        vm->resident[r->module] = after - before;
      }
    }
  }
  jerry_value_free(jerry_object_delete_sz(global, "__isere_register"));
  jerry_value_free(global);
  phase_us[ISERE_PHASE_MODULE] += __elapsed(&t);

  if (!vm->dead) {
    vm->arena_used = __heap_used();
  }
  __current_vm = outer;
  __current = outer_context;

  if (!ok || vm->dead) {
    js_vm_free(vm);
    return NULL;
  }
  return vm;
}

uint32_t js_vm_free(js_vm_t *vm)
{
  js_vm_t *outer = __current_vm;
  jerry_context_t *outer_context = __current;
  __current_vm = vm;
  __current = vm->context;

  if (vm->dead) {
    // the engine's state may be half-updated: drop the arena without touching it
    isere_free(vm->context);
  } else {
    jerry_value_free(vm->dispatch);
    for (uint32_t i = 0; i < isere_app.num_modules; i++) {
      if (vm->modules[i] != 0) {
        jerry_value_free(vm->modules[i]);
      }
    }
    // jerry_cleanup() releases the whole arena, so nothing can be left behind
    jerry_cleanup();
  }

  __current_vm = outer == vm ? NULL : outer;
  __current = outer_context == vm->context ? NULL : outer_context;
  isere_free(vm->modules);
  isere_free(vm->resident);
  isere_free(vm);
  return 0;
}

size_t js_vm_module_resident(js_vm_t *vm, int module)
{
  return module >= 0 ? vm->resident[module] : 0;
}

static int __dispatch(js_vm_t *vm, isere_request_t *req)
{
  const char *buf = req->buf;
  const http_request_head_t *head = req->head;

  // the one copy from the receive buffer into the engine, length-aware
  jerry_value_t headers = jerry_array((jerry_length_t)(head->num_headers * 2));
  for (size_t i = 0; i < head->num_headers; i++) {
    const http_header_t *h = &req->headers[i];
    jerry_value_t name = __string(buf + h->name.off, h->name.len);
    jerry_value_t value = __string(buf + h->value.off, h->value.len);
    jerry_value_free(jerry_object_set_index(headers, (jerry_length_t)(2 * i), name));
    jerry_value_free(jerry_object_set_index(headers, (jerry_length_t)(2 * i + 1), value));
    jerry_value_free(name);
    jerry_value_free(value);
  }

  jerry_value_t args[7] = {
    jerry_number(req->route),
    jerry_number(req->id),
    __string(buf + head->method.off, head->method.len),
    __string(buf + head->path.off, head->path.len),
    __string(buf + head->query.off, head->query.len),
    headers,
    __string(buf + req->body.off, req->body.len),
  };

  jerry_value_t undefined = jerry_undefined();
  jerry_value_t r = jerry_call(vm->dispatch, undefined, args, 7);
  for (int i = 0; i < 7; i++) {
    jerry_value_free(args[i]);
  }
  int ret = 0;
  if (jerry_value_is_exception(r)) {
    __print_exception(r, "dispatch");
    ret = -1;
  }
  jerry_value_free(r);
  return ret;
}

static int __run_job(js_vm_t *vm)
{
  // JerryScript runs the whole queue at once; it stops early only on an exception
  jerry_value_t r = jerry_run_jobs();
  int ran_into_error = jerry_value_is_exception(r);
  if (ran_into_error) {
    __print_exception(r, "job");
  }
  jerry_value_free(r);
  return ran_into_error ? 1 : 0;
}

static int __call(js_vm_t *vm, js_ref_t fn)
{
  // the callback may clear its own timer, which releases `fn`
  jerry_value_t f = jerry_value_copy(((jerry_ref_t *)fn)->value);
  jerry_value_t undefined = jerry_undefined();
  jerry_value_t r = jerry_call(f, undefined, NULL, 0);
  jerry_value_free(f);
  int ret = 0;
  if (jerry_value_is_exception(r)) {
    __print_exception(r, "timer");
    ret = -1;
  }
  jerry_value_free(r);
  return ret;
}

void js_vm_release(js_vm_t *vm, js_ref_t fn)
{
  if (fn == NULL) {
    return;
  }
  if (!vm->dead) {
    jerry_context_t *outer = __current;
    __current = vm->context;
    jerry_value_free(((jerry_ref_t *)fn)->value);
    __current = outer;
  }
  isere_free(fn);
}

void js_vm_gc(js_vm_t *vm)
{
  if (vm->dead) {
    return;
  }
  jerry_context_t *outer = __current;
  __current = vm->context;
  jerry_heap_gc(JERRY_GC_PRESSURE_HIGH);
  __current = outer;
}

static void __fetch_done(js_vm_t *vm, js_ref_t fn, const isere_fetch_result_t *result)
{
  jerry_value_t args[4];
  jerry_length_t argc;

  if (result->error != NULL) {
    args[0] = jerry_string_sz(result->error);
    argc = 1;
  } else {
    jerry_value_t headers = jerry_array((jerry_length_t)(result->num_headers * 2));
    for (size_t i = 0; i < result->num_headers; i++) {
      const http_header_t *h = &result->headers[i];
      jerry_value_t name = __string(result->buf + h->name.off, h->name.len);
      jerry_value_t value = __string(result->buf + h->value.off, h->value.len);
      jerry_value_free(jerry_object_set_index(headers, (jerry_length_t)(2 * i), name));
      jerry_value_free(jerry_object_set_index(headers, (jerry_length_t)(2 * i + 1), value));
      jerry_value_free(name);
      jerry_value_free(value);
    }
    args[0] = jerry_undefined();
    args[1] = jerry_number(result->status);
    args[2] = headers;
    args[3] = __uint8array((const uint8_t *)result->body, result->body_len);
    argc = 4;
  }

  jerry_value_t f = jerry_value_copy(((jerry_ref_t *)fn)->value);
  jerry_value_t undefined = jerry_undefined();
  jerry_value_t r = jerry_call(f, undefined, args, argc);
  jerry_value_free(f);
  for (jerry_length_t i = 0; i < argc; i++) {
    jerry_value_free(args[i]);
  }
  if (jerry_value_is_exception(r)) {
    __print_exception(r, "fetch callback");
  }
  jerry_value_free(r);
}

int js_vm_dead(js_vm_t *vm)
{
  return vm->dead;
}

int js_vm_dispatch(js_vm_t *vm, isere_request_t *req)
{
  int r = -1;
  if (!vm->dead) {
    GUARD(vm, r = __dispatch(vm, req), r = -1);
  }
  return r;
}

int js_vm_run_job(js_vm_t *vm)
{
  int r = 0;
  if (!vm->dead) {
    GUARD(vm, r = __run_job(vm), r = 0);
  }
  return r;
}

int js_vm_call(js_vm_t *vm, js_ref_t fn)
{
  int r = -1;
  if (!vm->dead) {
    GUARD(vm, r = __call(vm, fn), r = -1);
  }
  return r;
}

void js_vm_fetch_done(js_vm_t *vm, js_ref_t fn, const isere_fetch_result_t *result)
{
  if (!vm->dead) {
    GUARD(vm, __fetch_done(vm, fn, result), (void)0);
  }
}
