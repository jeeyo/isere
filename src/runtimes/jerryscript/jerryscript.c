#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript.h"

#include <string.h>
#include <stdint.h>

int js_runtime_init(isere_js_t *js)
{
  jerry_init(JERRY_INIT_EMPTY);
  return 0;
}

int js_runtime_deinit(isere_js_t *js)
{
  jerry_cleanup();
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
  const char *eval = "console.log('a');";
  jerry_value_t eval_ret = jerry_eval(eval, strlen(eval), JERRY_PARSE_NO_OPTS);
  if (!jerry_value_is_exception(eval_ret)) {
    jerry_value_free(eval_ret);
    return -1;
  }

  jerry_value_free(eval_ret);
  return 0;
}

int js_runtime_init_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return 0;
}

int js_runtime_deinit_context(isere_js_t *js, isere_js_context_t *ctx)
{
  return 0;
}

int js_runtime_poll(isere_js_context_t *ctx)
{
  return 0;
}
