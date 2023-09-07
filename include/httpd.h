#ifndef ISERE_HTTPD_H_

#define ISERE_HTTPD_H_

#include "isere.h"

#include <stdlib.h>
#include <stdint.h>

#include "llhttp.h"
#include "yuarel.h"

#include "FreeRTOS.h"
#include "task.h"

#define ISERE_HTTPD_PORT 8080
#define ISERE_HTTPD_LOG_TAG "httpd"

#define ISERE_HTTPD_MAX_CONNECTIONS 6
#define ISERE_HTTPD_HANDLER_TIMEOUT_MS 30000

#define ISERE_HTTPD_LINE_BUFFER_LEN 64

#define ISERE_HTTPD_MAX_HTTP_METHOD_LEN 16
#define ISERE_HTTPD_MAX_HTTP_PATH_LEN 256
#define ISERE_HTTPD_MAX_HTTP_HEADERS 20
#define ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN 64
#define ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN 1024
#define ISERE_HTTPD_MAX_HTTP_BODY_LEN 2048

#ifdef __cplusplus
extern "C" {
#endif

#define METHOD_COMPLETED (1 << 0)
#define PATH_COMPLETED (1 << 1)
#define HEADERS_COMPLETED (1 << 2)
#define BODY_COMPLETED (1 << 3)
#define ALL_COMPLETED (METHOD_COMPLETED | PATH_COMPLETED | HEADERS_COMPLETED | BODY_COMPLETED)

typedef struct {
  char name[ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN];
  char value[ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN];
} httpd_header_t;

typedef struct {

  int fd;

  llhttp_t llhttp;
  llhttp_settings_t llhttp_settings;

  struct yuarel url_parser;

  // parser state
  uint8_t completed;

  // HTTP method
  char method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN];

  // URL path
  char path[ISERE_HTTPD_MAX_HTTP_PATH_LEN];

  // HTTP headers
  httpd_header_t headers[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t num_header_fields;
  uint32_t num_header_values;

  // HTTP body
  char body[ISERE_HTTPD_MAX_HTTP_BODY_LEN];

  // Client connection task
  TaskHandle_t tsk;

} httpd_conn_t;

typedef int (httpd_handler_t)(
  isere_t *isere,
  httpd_conn_t *conn,
  const char *method,
  const char *path,
  const char *query,
  httpd_header_t *request_headers,
  uint32_t request_headers_len,
  const char *body);

typedef struct {
  isere_httpd_t *httpd;
  httpd_handler_t *handler;
} httpd_task_params_t;

typedef struct {
  httpd_conn_t *conn;
  httpd_handler_t *handler;
} httpd_client_task_params_t;

int httpd_init(isere_t *isere, isere_httpd_t *httpd);
int httpd_deinit(isere_httpd_t *httpd);
void httpd_task(void *params);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */
