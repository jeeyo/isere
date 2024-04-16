#ifndef ISERE_HTTPD_H_
#define ISERE_HTTPD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"
#include "tcp.h"
#include "js.h"

#include <stdlib.h>
#include <stdint.h>

#include "llhttp.h"
#include "yuarel.h"

#include "FreeRTOS.h"
#include "task.h"

#define ISERE_HTTPD_LOG_TAG "httpd"
#define ISERE_HTTPD_PORT 8080

#define ISERE_HTTPD_MAX_CONNECTIONS ISERE_TCP_MAX_CONNECTIONS
#define ISERE_HTTPD_HANDLER_TIMEOUT_MS 30000

#define ISERE_HTTPD_LINE_BUFFER_LEN 64

#define ISERE_HTTPD_MAX_HTTP_METHOD_LEN 16
#define ISERE_HTTPD_MAX_HTTP_PATH_LEN 256
#define ISERE_HTTPD_MAX_HTTP_HEADERS 16
#define ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN 64
#define ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN 512
#define ISERE_HTTPD_MAX_HTTP_BODY_LEN 512

#define ISERE_HTTPD_MAX_HTTP_REQUEST_LEN \
  (ISERE_HTTPD_MAX_HTTP_METHOD_LEN + ISERE_HTTPD_MAX_HTTP_PATH_LEN + ISERE_HTTPD_MAX_HTTP_HEADERS * (ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN + ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN) + ISERE_HTTPD_MAX_HTTP_BODY_LEN)

#define METHODED (1 << 0)
#define PATHED (1 << 1)
#define HEADERED (1 << 2)
#define BODYED (1 << 3)
#define POLLING (1 << 4)
#define PROCESSED (1 << 5)
#define PARSED (METHODED | PATHED | HEADERED | BODYED)
#define DONE (PARSED | POLLING | PROCESSED)

// #define HTTP_STATUS_BAD_REQUEST -400
// #define HTTP_STATUS_NOT_FOUND -404
// #define HTTP_STATUS_PAYLOAD_TOO_LARGE -413
// #define HTTP_STATUS_INTERNAL_SERVER_ERROR -500

typedef struct {
  char name[ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN];
  char value[ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN];
} httpd_header_t;

typedef struct {

  tcp_socket_t *socket;
  int32_t recvd;  // number of bytes received

  isere_js_t js;

  llhttp_t llhttp;
  llhttp_settings_t llhttp_settings;

  struct yuarel url_parser;

  uint8_t completed;  // bitfield of completed parts of the request

  char method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN]; // GET, POST, etc.
  char path[ISERE_HTTPD_MAX_HTTP_PATH_LEN]; // /foo/bar
  char body[ISERE_HTTPD_MAX_HTTP_BODY_LEN]; // request body

  httpd_header_t headers[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t num_header_fields;
  uint32_t num_header_values;

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

int isere_httpd_init(isere_t *isere, isere_httpd_t *httpd, httpd_handler_t *handler);
int isere_httpd_deinit(isere_httpd_t *httpd);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */
