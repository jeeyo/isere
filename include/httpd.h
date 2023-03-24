#ifndef ISERE_HTTPD_H_

#define ISERE_HTTPD_H_

#include "isere.h"

#include <stdlib.h>
#include <stdint.h>

#define ISERE_HTTPD_PORT 8080
#define ISERE_HTTPD_LOG_TAG "httpd"

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

int httpd_init(isere_t *isere);
int httpd_deinit(int fd);
void httpd_task(void *params);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */