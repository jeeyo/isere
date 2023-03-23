#include "httpd.h"

#include "js.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "FreeRTOS.h"
#include "task.h"

#include "llhttp.h"

// #include "lwip/err.h"
// #include "lwip/errno.h"
// #include "lwip/sockets.h"
// #include "lwip/sys.h"
// #include "lwip/netdb.h"

static isere_t *__isere = NULL;
static llhttp_t __llhttp;
static llhttp_settings_t __llhttp_settings;

static char *__method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN];
static int __headers_parsed = 0;
static int __no_of_header_fields_parsed = 0;
static httpd_header_t __headers[ISERE_HTTPD_MAX_HTTP_HEADERS];

static int __on_method(llhttp_t *parser, const char *at, size_t length)
{
  __isere->logger->info("[%s] method: %*.s", ISERE_HTTPD_LOG_TAG, length, at);
  return 0;
}

static int __on_header_field(llhttp_t *parser, const char *at, size_t length)
{
  if (__no_of_header_fields_parsed < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &__headers[__no_of_header_fields_parsed].name_len;
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(__headers[__no_of_header_fields_parsed].name + *current_length, at, length);
    *current_length += length;
  }
  return 0;
}

static int __on_header_field_complete(llhttp_t *parser)
{
  if (__no_of_header_fields_parsed < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &__headers[__no_of_header_fields_parsed].name_len;
    __headers[__no_of_header_fields_parsed].name[*current_length] = '\0';
  }
  return 0;
}

static int __on_header_value(llhttp_t *parser, const char *at, size_t length)
{
  if (__no_of_header_fields_parsed < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &__headers[__no_of_header_fields_parsed].value_len;
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(__headers[__no_of_header_fields_parsed].value + *current_length, at, length);
    *current_length += length;
  }
  return 0;
}

static int __on_header_value_complete(llhttp_t *parser)
{
  if (__no_of_header_fields_parsed < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &__headers[__no_of_header_fields_parsed].value_len;
    __headers[__no_of_header_fields_parsed].value[*current_length] = '\0';
    __no_of_header_fields_parsed++;
  }
  return 0;
}

static int __on_headers_complete(llhttp_t *parser)
{
  __headers_parsed = 1;

  for (int i = 0; i < __no_of_header_fields_parsed; i++) {
    __isere->logger->info("[%s] header: %s (%d): %s (%d)", ISERE_HTTPD_LOG_TAG, __headers[i].name, __headers[i].name_len, __headers[i].value, __headers[i].value_len);
  }

  return 0;
}

int httpd_init(isere_t *isere)
{
  __isere = isere;

  llhttp_settings_init(&__llhttp_settings);
  // __llhttp_settings.on_method = __on_method;
  __llhttp_settings.on_header_field = __on_header_field;
  __llhttp_settings.on_header_value = __on_header_value;
  __llhttp_settings.on_header_field_complete = __on_header_field_complete;
  __llhttp_settings.on_header_value_complete = __on_header_value_complete;
  __llhttp_settings.on_headers_complete = __on_headers_complete;
  // __llhttp_settings.on_message_complete = handle_on_message_complete;
  llhttp_init(&__llhttp, HTTP_REQUEST, &__llhttp_settings);

  return 0;
}

int httpd_deinit(int fd)
{
  if (__isere) {
    __isere = NULL;
  }

  // close(fd);
  return 0;
}

static int __httpd_read_and_parse(int sock)
{
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  for(;;) {

    // clear line buffer
    memset(linebuf, 0, sizeof(linebuf));

    int len = recv(sock, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN, 0);
    if (len < 0) {
      __isere->logger->error("[%s] recv() error: %s", ISERE_HTTPD_LOG_TAG, strerror(errno));
      goto cleanup;
    }

    if (len == 0) {
      __isere->logger->info("[%s] recv(): returned zero", ISERE_HTTPD_LOG_TAG);
      goto cleanup;
    }

    enum llhttp_errno err = llhttp_execute(&__llhttp, linebuf, len);
    if (err != HPE_OK) {
      __isere->logger->error("[%s] llhttp_execute() error: %s %s", ISERE_HTTPD_LOG_TAG, llhttp_errno_name(err), __llhttp.reason);
      goto cleanup;
    }
  }

cleanup:
  llhttp_finish(&__llhttp);
  return 0;
}

void httpd_task(void *params)
{
  httpd_handler_t *handler = (httpd_handler_t *)params;

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_sock < 0) {
    __isere->logger->error("[%s] socket() error: %s", ISERE_HTTPD_LOG_TAG, strerror(errno));
    vTaskDelete(NULL);
    return;
  }

  // int opt = 1;
  // setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(ISERE_HTTPD_PORT);

  int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    __isere->logger->error("[%s] bind() error: %s", ISERE_HTTPD_LOG_TAG, strerror(errno));
    goto cleanup;
  }

  err = listen(listen_sock, 1);
  if (err != 0) {
    __isere->logger->error("[%s] listen() error: %s", ISERE_HTTPD_LOG_TAG, strerror(errno));
    goto cleanup;
  }

  __isere->logger->info("[%s] Listening on port %d", ISERE_HTTPD_LOG_TAG, ISERE_HTTPD_PORT);

  for (;;) {

    // accept connection
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    int sock = -1;

    sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
    if (sock < 0 && errno == EINTR) {
      continue;
    }

    if (sock < 0) {
      __isere->logger->error("[%s] accept() error: %s (%d)", ISERE_HTTPD_LOG_TAG, strerror(errno));
      break;
    }

    // disable tcp keepalive
    int keep_alive = 0;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

    // convert ip address to string
    __isere->logger->info("[%s] Received connection from %s", ISERE_HTTPD_LOG_TAG, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr));

    // parse http request
    memset(__method, 0, sizeof(__method));
    memset(__headers, 0, sizeof(__headers));
    __httpd_read_and_parse(sock);
  }

cleanup:
  close(listen_sock);
  vTaskDelete(NULL);
}
