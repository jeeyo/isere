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

struct __parsing_state {
  size_t method_len;
  size_t path_len;
  size_t header_name_len[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t current_header_name_index;
  size_t header_value_len[ISERE_HTTPD_MAX_HTTP_HEADERS];
  uint32_t current_header_value_index;
  size_t body_len;

  int method_complete;
  int path_complete;
  int headers_complete;
  int body_complete;
} __state;

static char __method[ISERE_HTTPD_MAX_HTTP_METHOD_LEN];
static char __path[ISERE_HTTPD_MAX_HTTP_PATH_LEN];
static httpd_header_t __headers[ISERE_HTTPD_MAX_HTTP_HEADERS];

static int __on_method(llhttp_t *parser, const char *at, size_t length)
{
  // boundary check
  size_t *current_length = &__state.method_len;
  size_t available = ISERE_HTTPD_MAX_HTTP_METHOD_LEN - *current_length - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(__method + *current_length, at, length);
  *current_length += length;
  return 0;
}

static int __on_method_complete(llhttp_t *parser)
{
  // __isere->logger->info("[%s] method: %s", ISERE_HTTPD_LOG_TAG, __method);
  __state.method_complete = 1;
  return 0;
}

static int __on_url(llhttp_t *parser, const char *at, size_t length)
{
  // boundary check
  size_t *current_length = &__state.path_len;
  size_t available = ISERE_HTTPD_MAX_HTTP_PATH_LEN - *current_length - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(__path + *current_length, at, length);
  *current_length += length;
  return 0;
}

static int __on_url_complete(llhttp_t *parser)
{
  // __isere->logger->info("[%s] path: %s", ISERE_HTTPD_LOG_TAG, __path);
  __state.path_complete = 1;
  return 0;
}

static int __on_header_field(llhttp_t *parser, const char *at, size_t length)
{
  if (__state.current_header_name_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &__state.header_name_len[__state.current_header_name_index];
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(__headers[__state.current_header_name_index].name + *current_length, at, length);
    *current_length += length;
  }

  return 0;
}

static int __on_header_field_complete(llhttp_t *parser)
{
  if (__state.current_header_name_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &__state.header_name_len[__state.current_header_name_index];
    __headers[__state.current_header_name_index].name[*current_length] = '\0';
    __state.current_header_name_index++;
  }

  return 0;
}

static int __on_header_value(llhttp_t *parser, const char *at, size_t length)
{
  if (__state.current_header_value_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &__state.header_value_len[__state.current_header_value_index];
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(__headers[__state.current_header_value_index].value + *current_length, at, length);
    *current_length += length;
  }

  return 0;
}

static int __on_header_value_complete(llhttp_t *parser)
{
  if (__state.current_header_value_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &__state.header_value_len[__state.current_header_value_index];
    __headers[__state.current_header_value_index].value[*current_length] = '\0';
    __state.current_header_value_index++;
  }

  return 0;
}

static int __on_headers_complete(llhttp_t *parser)
{
  // for (int i = 0; i < ISERE_HTTPD_MAX_HTTP_HEADERS; i++) {
  //   httpd_header_t *header = &__headers[i];
  //   if (__state.header_name_len[i] == 0 || __state.header_value_len[i] == 0) {
  //     break;
  //   }

  //   __isere->logger->info("[%s] header: %s: %s", ISERE_HTTPD_LOG_TAG, header->name, header->value);
  // }

  __state.headers_complete = 1;

  return 0;
}

int httpd_init(isere_t *isere)
{
  __isere = isere;
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

    if (__state.method_complete && __state.path_complete && __state.headers_complete) {
      break;
    }

    // clear line buffer
    memset(linebuf, 0, sizeof(linebuf));

    int len = recv(sock, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN, 0);
    if (len < 0) {

      if (errno == EINTR) {
        continue;
      }

      __isere->logger->error("[%s] recv() error: %s", ISERE_HTTPD_LOG_TAG, strerror(errno));
      return -1;
    }

    if (len == 0) {
      __isere->logger->info("[%s] recv(): returned zero", ISERE_HTTPD_LOG_TAG);
      return 0;
    }

    enum llhttp_errno err = llhttp_execute(&__llhttp, linebuf, len);
    if (err != HPE_OK) {
      __isere->logger->error("[%s] llhttp_execute() error: %s %s", ISERE_HTTPD_LOG_TAG, llhttp_errno_name(err), __llhttp.reason);
      return -1;
    }
  }

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

    // initialize llhttp
    llhttp_settings_init(&__llhttp_settings);
    __llhttp_settings.on_method = __on_method;
    __llhttp_settings.on_url = __on_url;
    __llhttp_settings.on_url_complete = __on_url_complete;
    __llhttp_settings.on_method_complete = __on_method_complete;
    __llhttp_settings.on_header_field = __on_header_field;
    __llhttp_settings.on_header_value = __on_header_value;
    __llhttp_settings.on_header_field_complete = __on_header_field_complete;
    __llhttp_settings.on_header_value_complete = __on_header_value_complete;
    __llhttp_settings.on_headers_complete = __on_headers_complete;
    // __llhttp_settings.on_body = __on_body;
    // __llhttp_settings.on_message_complete = handle_on_message_complete;
    llhttp_init(&__llhttp, HTTP_REQUEST, &__llhttp_settings);

    // convert ip address to string
    __isere->logger->info("[%s] Received connection from %s", ISERE_HTTPD_LOG_TAG, inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr));

    // read and parse http request
    memset(&__state, 0, sizeof(__state));
    memset(__method, 0, sizeof(__method));
    memset(__headers, 0, sizeof(__headers));
    __httpd_read_and_parse(sock);
    llhttp_finish(&__llhttp);

    uint32_t nbr_of_headers = MIN(__state.current_header_name_index, __state.current_header_value_index);
    handler(__method, __path, __headers, nbr_of_headers);

    const char *buf = "HTTP/1.1 200 OK\r\n\r\nTest\r\n\r\n";
    write(sock, buf, strlen(buf));
    close(sock);
  }

cleanup:
  close(listen_sock);
  vTaskDelete(NULL);
}
