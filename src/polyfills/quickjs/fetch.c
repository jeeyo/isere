#include "polyfills.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

static JSValue __polyfill_fetch_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  int64_t delay;
  JSValueConst url, method;

  url = argv[0];
  method = argv[1];

  return JS_TRUE;
}

void isere_js_polyfill_fetch_init(isere_js_context_t *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  JS_SetPropertyStr(ctx->context, global_obj, "__fetch", JS_NewCFunction(ctx->context, __polyfill_fetch_internal, "__fetch", 1));
  JS_FreeValue(ctx->context, global_obj);

  // TODO: support standard Fetch Request object (https://developer.mozilla.org/en-US/docs/Web/API/Request)
  const char *str = "globalThis.fetch = async function(resource) {\n"
                    "  let url = null\n"
                    "  if (typeof resource === 'string') url = resource\n"
                    "  else if (typeof resource === 'object' && typeof resource.url === 'string') url = resource.url\n"
                    "  else if (typeof resource === 'object') url = resource.toString()\n"

                    "  let method = 'GET'\n"
                    "  if (typeof resource === 'object' && typeof resource.method === 'string') method = resource.method\n"

                    "  return globalThis.__fetch(url, method);\n"
                    "};";
  JS_Eval(ctx->context, str, strlen(str), "<polyfills>", JS_EVAL_TYPE_MODULE);
}

void isere_js_polyfill_fetch_deinit(isere_js_context_t *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  JS_DeleteProperty(ctx->context, global_obj, JS_NewAtom(ctx->context, "__fetch"), 0);
  JS_FreeValue(ctx->context, global_obj);
}

int isere_js_polyfill_fetch_poll(isere_js_context_t *ctx)
{
  return 1;
}
