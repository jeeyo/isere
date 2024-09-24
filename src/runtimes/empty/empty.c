#include "isere.h"

#include "runtime.h"

int js_runtime_init(isere_js_t *js)
{
  return 0;
}

int js_runtime_deinit(isere_js_t *js)
{
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
