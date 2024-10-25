#include "js.h"
#include "runtime.h"
#include "polyfills.h"

#include "quickjs.h"
#include "quickjs-libc.h"

#include <string.h>

static isere_t *__isere = NULL;

int isere_js_init(isere_t *isere, isere_js_t *js)
{
  __isere = isere;
  return js_runtime_init(js);
}

int isere_js_deinit(isere_js_t *js)
{
  return js_runtime_deinit(js);
}

int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return js_runtime_init_context(js, ctx);
}

int isere_js_free_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return js_runtime_deinit_context(js, ctx);
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

int isere_js_eval(
  isere_js_context_t *ctx,
  unsigned char *handler,
  unsigned int handler_len,
  const char *method,
  const char *path,
  const char *query,
  const char (*request_header_names)[],
  const char (*request_header_values)[],
  const uint32_t request_headers_len,
  const char *body)
{
  if (js_runtime_eval_handler(
    ctx,
    handler,
    handler_len,
    method,
    path,
    query,
    (const char **)request_header_names,
    (const char **)request_header_values,
    request_headers_len,
    body) < 0) {
    // TODO: error goes to logger
    return -1;
  }

  return 0;
}

int isere_js_poll(isere_js_context_t *ctx)
{
  return js_runtime_poll(ctx);
}
