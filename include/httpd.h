#ifndef ISERE_HTTPD_H_

#define ISERE_HTTPD_H_

#include "isere.h"

#include <stdlib.h>
#include <stdint.h>

#include "llhttp.h"
// #include "event_groups.h"

#define ISERE_HTTPD_PORT 8080
#define ISERE_HTTPD_LOG_TAG "httpd"

#define ISERE_HTTPD_MAX_CONNECTIONS 10

#define ISERE_HTTPD_LINE_BUFFER_LEN 64

#define ISERE_HTTPD_MAX_HTTP_METHOD_LEN 16
#define ISERE_HTTPD_MAX_HTTP_PATH_LEN 64
#define ISERE_HTTPD_MAX_HTTP_HEADERS 20
#define ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN 64
#define ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN 1024

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char name[ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN];
  char value[ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN];
} httpd_header_t;

typedef int (httpd_handler_t)(isere_t *isere, const char *method, const char *path, httpd_header_t *request_headers, uint32_t request_headers_len);

typedef struct {

  int fd;

  llhttp_t llhttp;
  llhttp_settings_t llhttp_settings;

  // parser state
  // HTTP method
  char method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN];
  size_t method_len;
  int method_complete;

  // URL path
  char path[ISERE_HTTPD_MAX_HTTP_PATH_LEN];
  size_t path_len;
  int path_complete;

  // HTTP headers
  httpd_header_t headers[ISERE_HTTPD_MAX_HTTP_HEADERS];
  size_t header_name_len[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t current_header_name_index;
  size_t header_value_len[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t current_header_value_index;
  int headers_complete;

  // size_t body_len;
  // int body_complete;
} isere_httpd_connection_t;

typedef struct {
  isere_httpd_t *httpd;
  httpd_handler_t *handler;
  // EventGroupHandle_t rxne;
} httpd_task_params_t;

int httpd_init(isere_t *isere, isere_httpd_t *httpd);
int httpd_deinit(isere_httpd_t *httpd);
void httpd_task(void *params);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */
