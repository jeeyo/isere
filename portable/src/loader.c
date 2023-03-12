#include "loader.h"
#include "logger.h"

#include <string.h>

// #include <dlfcn.h>
#include "quickjs.h"
#include "quickjs-libc.h"

static JSRuntime *__qjs_runtime = NULL;
static JSContext *__qjs_context = NULL;
static isere_logger_t *__isere_logger = NULL;
static FILE *__isere_function_file = NULL;

static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
  if (__isere_logger == NULL) {
    return JS_EXCEPTION;
  }

  const char *str;
  size_t len;

  for(int i = 0; i < argc; i++) {

    // add space between arguments
    if (i != 0) {
      __isere_logger->debug(" ");
    }

    // convert argument to C string
    str = JS_ToCStringLen(ctx, &len, argv[i]);
    if (!str) {
      return JS_EXCEPTION;
    }

    // print string using logger
    __isere_logger->debug("%s", str);

    JS_FreeCString(ctx, str);
  }

  __isere_logger->debug("\n");

  return JS_UNDEFINED;
}

void *loader_open(isere_logger_t *logger)
{
  // return dlopen(filename, RTLD_LAZY);

  // __isere_function_file = fopen("./examples/echo.cjs.js", "r");

  __isere_logger = logger;

  __qjs_runtime = JS_NewRuntime();
  __qjs_context = JS_NewContext(__qjs_runtime);

  JSValue global_obj, console;
  global_obj = JS_GetGlobalObject(__qjs_context);

  console = JS_NewObject(__qjs_context);
  JS_SetPropertyStr(__qjs_context, console, "log",
                    JS_NewCFunction(__qjs_context, js_print, "log", 1));
  JS_SetPropertyStr(__qjs_context, global_obj, "console", console);

  JS_FreeValue(__qjs_context, global_obj);

  return __qjs_context;
}

int loader_close(void *ctx)
{
  // return dlclose(handle);

  if (__isere_logger) {
    __isere_logger = NULL;
  }

  if (__isere_function_file) {
    fclose(__isere_function_file);
    __isere_function_file = NULL;
  }

  if (__qjs_context) {
    JS_FreeContext(__qjs_context);
    __qjs_context = NULL;
  }

  if (__qjs_runtime) {
    JS_FreeRuntime(__qjs_runtime);
    __qjs_runtime = NULL;
  }

  return 0;
}

loader_fn_t *loader_get_fn(void *ctx, const char *fn)
{
  // return (loader_fn_t *)(dlsym(handle, fn));

  const char *buf = "console.log('Hello World from QuickJs!')";
  JSValue val;
  int ret;

  val = JS_Eval(ctx, buf, strlen(buf), "<cmdline>", 0);

  if (JS_IsException(val)) {
    js_std_dump_error(ctx);
    ret = -1;
  } else {
    ret = 0;
  }
  JS_FreeValue(ctx, val);
  // return ret;
  return NULL;
}

// char *loader_last_error()
// {
//   return dlerror();
// }
