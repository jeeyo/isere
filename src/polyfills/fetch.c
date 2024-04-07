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

void polyfill_fetch_init(JSContext *ctx)
{
  /*
    Resource object
    (https://developer.mozilla.org/en-US/docs/Web/API/Request)
    ```
    {
      "url": "https://example.com",
      "method": "GET",
      "headers": {
        "header1Name": "header1Value",
        "header2Name": "header2Value",
      },
      "body": "...",
    }
    ```
  */

  JSValue global_obj = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global_obj, "__fetch", JS_NewCFunction(ctx, __polyfill_fetch_internal, "__fetch", 1));
  JS_FreeValue(ctx, global_obj);

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
  JS_Eval(ctx, str, strlen(str), "<polyfills>", JS_EVAL_TYPE_MODULE);
}

void polyfill_fetch_deinit(JSContext *ctx)
{
  JSValue global_obj = JS_GetGlobalObject(ctx);
  JS_DeleteProperty(ctx, global_obj, JS_NewAtom(ctx, "__fetch"), 0);
  JS_FreeValue(ctx, global_obj);
}

int polyfill_fetch_poll(JSContext *ctx)
{
  return 1;
}
