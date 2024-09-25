#include <string.h>
#include <unistd.h>

#include "isere.h"
#include "httpd.h"
#include "tcp.h"

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

  // TODO: optimize this for smaller ISERE_HTTPD_TASK_STACK_SIZE
  char header_names[ISERE_HTTPD_MAX_HTTP_HEADERS][ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN] = {0};
  char header_values[ISERE_HTTPD_MAX_HTTP_HEADERS][ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN] = {0};
  for (int i = 0; i < request_headers_len; i++) {
    strncpy(header_names[i], request_headers[i].name, ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN);
    strncpy(header_values[i], request_headers[i].value, ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN);
  }

  // evaluate handler function
  // TODO: make this async
  int ret = isere_js_eval(
    ctx,
    isere->loader->fn,
    isere->loader->fn_size,
    method,
    path,
    query,
    (const char **)header_names,
    (const char **)header_values,
    request_headers_len,
    body);
  if (ret < 0) {
    // TODO: this should be logged
  }

  return 0;
}
