#ifndef ISERE_HTTPD_H_
#define ISERE_HTTPD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "loader.h"
#include "tcp.h"
#include "js.h"
#include "otel.h"

#include <stdlib.h>
#include <stdint.h>

#include "llhttp.h"
#include "yuarel.h"
#include "libuv/uv.h"

#include "FreeRTOS.h"
#include "task.h"

#define ISERE_HTTPD_LOG_TAG "httpd"
#define ISERE_HTTPD_PORT 8080

#define ISERE_HTTPD_HANDLER_TIMEOUT_MS 30000

#define ISERE_HTTPD_LINE_BUFFER_LEN 64

#define ISERE_HTTPD_MAX_HTTP_METHOD_LEN 16
#define ISERE_HTTPD_MAX_HTTP_PATH_LEN 256
#define ISERE_HTTPD_MAX_HTTP_HEADERS 16
#define ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN 64
#define ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN 512
#define ISERE_HTTPD_MAX_HTTP_BODY_LEN 512

#define ISERE_HTTPD_MAX_HTTP_REQUEST_LEN \
  (ISERE_HTTPD_MAX_HTTP_METHOD_LEN + \
    ISERE_HTTPD_MAX_HTTP_PATH_LEN + \
    ISERE_HTTPD_MAX_HTTP_HEADERS * (ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN + ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN) + \
    ISERE_HTTPD_MAX_HTTP_BODY_LEN)

#ifndef ISERE_HTTPD_TASK_STACK_SIZE
#define ISERE_HTTPD_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE
#endif /* ISERE_HTTPD_TASK_STACK_SIZE */

typedef struct {
  char name[ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN];
  char value[ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN];
} httpd_header_t;

typedef struct {
  uint8_t completed;
  uint32_t statusCode;

  char *header_names[ISERE_HTTPD_MAX_HTTP_HEADERS];
  char *header_values[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t num_header_fields;

  char *body;
  size_t body_len;
} httpd_response_object_t;

typedef struct {
  int fd;
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];
  int linebuflen;
  int32_t recvd;  // total number of bytes received

  uv__io_t w;
  isere_js_context_t js;
  struct uv__queue js_queue;

  llhttp_t llhttp;
  llhttp_settings_t llhttp_settings;

  struct yuarel url_parser;

  // request
  char method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN]; // GET, POST, etc.
  char path[ISERE_HTTPD_MAX_HTTP_PATH_LEN]; // /foo/bar
  char body[ISERE_HTTPD_MAX_HTTP_BODY_LEN]; // request body

  httpd_header_t headers[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t num_header_fields;
  uint32_t num_header_values;

  // response
  httpd_response_object_t response;

} httpd_conn_t;

typedef int (httpd_handler_t)(
  isere_loader_t *loader,
  httpd_conn_t *conn,
  const char *method,
  const char *path,
  const char *query,
  httpd_header_t *request_headers,
  uint32_t request_headers_len,
  const char *body);

typedef struct {
  uint8_t should_exit;
  TaskHandle_t tsk;
  int serverfd;
  uv__io_t w;
  uv_loop_t loop;
  struct uv__queue js_queue;

  otel_metrics_counter_t *connections_counter;

  isere_logger_t *logger;
  isere_loader_t *loader;
  httpd_handler_t *http_handler;
  isere_js_t *js;
} isere_httpd_t;

int isere_httpd_init(isere_httpd_t *httpd,
  isere_loader_t *loader,
  isere_logger_t *logger,
  isere_js_t *js,
  httpd_handler_t *handler);
int isere_httpd_deinit(isere_httpd_t *httpd);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */
