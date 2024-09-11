#include <string.h>
#include <unistd.h>

#include "isere.h"
#include "js.h"
#include "httpd.h"
#include "tcp.h"

#include "runtime.h"

int __http_handler(
  isere_t *isere,
  httpd_conn_t *conn,
  const char *method,
  const char *path,
  const char *query,
  httpd_header_t *request_headers,
  uint32_t request_headers_len,
  const char *body)
{
  isere_js_context_t *ctx = &conn->js;

  js_runtime_populate_params_to_globalobj(ctx, method, path, query, request_headers, request_headers_len, body);

  // evaluate handler function
  // TODO: make this async
  int ret = isere_js_eval(ctx, isere->loader->fn, isere->loader->fn_size);
  if (ret < 0) {
    // TODO: this should be logged
  }

  return 0;
}
