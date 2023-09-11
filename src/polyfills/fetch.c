#include "polyfills.h"

#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

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
  const char *str = "globalThis.fetch = async function(resource) {\n"
                    "  return resource;\n"
                    "};";
  JS_Eval(ctx, str, strlen(str), "<fetch>", JS_EVAL_TYPE_MODULE);
}

void polyfill_fetch_deinit(JSContext *ctx)
{
}

JSValue polyfill_fetch_internal(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  int64_t delay;
  JSValueConst res;
  JSValue obj;

  res = argv[0];

  // `resource` must be an object
  if (!JS_IsObject(res)) {
    return JS_ThrowTypeError(ctx, "not an object");
  }

  return obj;
}

int polyfill_fetch_poll(JSContext *ctx)
{
  return 1;
}
