#include "loader.h"

#include <string.h>

#include <dlfcn.h>
#include "quickjs.h"
#include "quickjs-libc.h"

static JSRuntime *__qjs_runtime = NULL;
static JSContext *__qjs_context = NULL;
static isere_t *__isere = NULL;
static void *__dynlnk = NULL;

static JSValue __console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  if (__isere->logger == NULL) {
    return JS_EXCEPTION;
  }

  for (int i = 0; i < argc; i++) {
    // add space between arguments
    if (i != 0)
    {
      __isere->logger->debug(" ");
    }

    // convert argument to C string
    size_t len;
    const char *str = JS_ToCStringLen(ctx, &len, argv[i]);
    if (!str) {
      return JS_EXCEPTION;
    }

    // print string using logger
    __isere->logger->debug("%s", str);

    JS_FreeCString(ctx, str);
  }

  __isere->logger->debug("\n");

  return JS_UNDEFINED;
}

int loader_init(isere_t *isere)
{
  __isere = isere;

  // initialize quickjs runtime
  __qjs_runtime = JS_NewRuntime();
  // TODO: custom memory allocation with JS_NewRuntime2()
  // TODO: set global memory limit with JS_SetMemoryLimit()
  if (!__qjs_runtime)
  {
    isere->logger->error("failed to create QuickJS runtime");
    return -1;
  }
  JS_SetMaxStackSize(__qjs_runtime, ISERE_LOADER_STACK_SIZE);

  // initialize quickjs context
  __qjs_context = JS_NewContextRaw(__qjs_runtime);
  if (!__qjs_context)
  {
    isere->logger->error("failed to create QuickJS context");
    return -2;
  }
  JS_AddIntrinsicBaseObjects(__qjs_context);
  JS_AddIntrinsicDate(__qjs_context);
  JS_AddIntrinsicEval(__qjs_context);
  JS_AddIntrinsicStringNormalize(__qjs_context);
  JS_AddIntrinsicRegExp(__qjs_context);
  JS_AddIntrinsicJSON(__qjs_context);
  JS_AddIntrinsicProxy(__qjs_context);
  JS_AddIntrinsicMapSet(__qjs_context);
  JS_AddIntrinsicTypedArrays(__qjs_context);
  JS_AddIntrinsicPromise(__qjs_context);
  JS_AddIntrinsicBigInt(__qjs_context);

  JSValue global_obj = JS_GetGlobalObject(__qjs_context);
  // add console.log() function
  JSValue console = JS_NewObject(__qjs_context);
  JS_SetPropertyStr(__qjs_context, console, "log", JS_NewCFunction(__qjs_context, __console_log, "log", 1));
  JS_SetPropertyStr(__qjs_context, global_obj, "console", console);

  // add process.env
  JSValue process = JS_NewObject(__qjs_context);
  JSValue env = JS_NewObject(__qjs_context);
  JS_SetPropertyStr(__qjs_context, env, "NODE_ENV", JS_NewString(__qjs_context, "production"));
  JS_SetPropertyStr(__qjs_context, process, "env", env);
  JS_SetPropertyStr(__qjs_context, global_obj, "process", process);

  JS_FreeValue(__qjs_context, global_obj);

  return 0;
}

int loader_open(const char *filename)
{
  __dynlnk = dlopen(filename, RTLD_LAZY);
  if (!__dynlnk)
  {
    return -1;
  }

  return 0;
}

int loader_close()
{
  if (__isere)
  {
    __isere = NULL;
  }

  if (__qjs_context)
  {
    JS_FreeContext(__qjs_context);
    __qjs_context = NULL;
  }

  if (__qjs_runtime)
  {
    JS_FreeRuntime(__qjs_runtime);
    __qjs_runtime = NULL;
  }

  if (__dynlnk) {
    dlclose(__dynlnk);
    __dynlnk = NULL;
  }

  return 0;
}

loader_fn_t *loader_get_fn(uint32_t *size)
{
  *size = *(uint32_t *)(dlsym(__dynlnk, ISERE_LOADER_HANDLER_SIZE_FUNCTION));
  return (loader_fn_t *)(dlsym(__dynlnk, ISERE_LOADER_HANDLER_FUNCTION));
}

int loader_eval_fn(loader_fn_t *fn, uint32_t fn_size)
{
  const char *eval = "import { handler } from 'handler';\n"
    "handler().then((k) => console.log(JSON.stringify(k, null, 2)));";

  JSValue val = JS_Eval(__qjs_context, (const char *)fn, fn_size, "handler", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(val))
  {
    JS_FreeValue(__qjs_context, val);
    return -1;
  }

  js_module_set_import_meta(__qjs_context, val, 0, 0);
  JSModuleDef *m = val.u.ptr;

  val = JS_Eval(__qjs_context, eval, strlen(eval), "<cmdline>", JS_EVAL_TYPE_MODULE);
  if (JS_IsException(val))
  {
    js_std_dump_error(__qjs_context);
    JS_FreeValue(__qjs_context, val);
    return -1;
  }
  JS_FreeValue(__qjs_context, val);
  
  js_std_loop(__qjs_context);

  return 0;
}

char *loader_last_error()
{
  // TODO: get last error from quickjs context
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

  return dlerror();
}
