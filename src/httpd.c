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

// #include "lwip/err.h"
// #include "lwip/errno.h"
// #include "lwip/sockets.h"
// #include "lwip/sys.h"
// #include "lwip/netdb.h"

static isere_t *__isere = NULL;
static isere_httpd_conn_t __conns[ISERE_HTTPD_MAX_CONNECTIONS];

static int __on_method(llhttp_t *parser, const char *at, size_t length)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  // boundary check
  size_t *current_length = &conn->method_len;
  size_t available = ISERE_HTTPD_MAX_HTTP_METHOD_LEN - *current_length - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->method + *current_length, at, length);
  *current_length += length;
  return 0;
}

static int __on_method_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;
  conn->method_complete = 1;
  return 0;
}

static int __on_url(llhttp_t *parser, const char *at, size_t length)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  // boundary check
  size_t *current_length = &conn->path_len;
  size_t available = ISERE_HTTPD_MAX_HTTP_PATH_LEN - *current_length - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->path + *current_length, at, length);
  *current_length += length;
  return 0;
}

static int __on_url_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;
  yuarel_parse(&conn->url_parser, conn->path);
  conn->path_complete = 1;
  return 0;
}

static int __on_header_field(llhttp_t *parser, const char *at, size_t length)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  if (conn->current_header_name_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &conn->header_name_len[conn->current_header_name_index];
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(conn->headers[conn->current_header_name_index].name + *current_length, at, length);
    *current_length += length;
  }

  return 0;
}

static int __on_header_field_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  if (conn->current_header_name_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &conn->header_name_len[conn->current_header_name_index];
    conn->headers[conn->current_header_name_index].name[*current_length] = '\0';
    conn->current_header_name_index++;
  }

  return 0;
}

static int __on_header_value(llhttp_t *parser, const char *at, size_t length)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  if (conn->current_header_value_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t *current_length = &conn->header_value_len[conn->current_header_value_index];
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN - *current_length - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(conn->headers[conn->current_header_value_index].value + *current_length, at, length);
    *current_length += length;
  }

  return 0;
}

static int __on_header_value_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  if (conn->current_header_value_index < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t *current_length = &conn->header_value_len[conn->current_header_value_index];
    conn->headers[conn->current_header_value_index].value[*current_length] = '\0';
    conn->current_header_value_index++;
  }

  return 0;
}

static int __on_headers_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;
  conn->headers_complete = 1;
  return 0;
}

static int __on_body(llhttp_t *parser, const char *at, size_t length)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;

  // boundary check
  size_t *current_length = &conn->body_len;
  size_t available = ISERE_HTTPD_MAX_HTTP_BODY_LEN - *current_length - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->body + *current_length, at, length);
  *current_length += length;
  return 0;
}

static int handle_on_message_complete(llhttp_t *parser)
{
  isere_httpd_conn_t *conn = (isere_httpd_conn_t *)parser->data;
  conn->body_complete = 1;
  return 0;
}

int httpd_init(isere_t *isere, isere_httpd_t *httpd)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    memset(&__conns[i], 0, sizeof(isere_httpd_conn_t));
    __conns[i].fd = -1;
  }

  return 0;
}

int httpd_deinit(isere_httpd_t *httpd)
{
  if (__isere) {
    __isere = NULL;
  }

  close(httpd->server_fd);
  return 0;
}

static int __httpd_read_and_parse(isere_httpd_conn_t *conn)
{
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  for(;;) {

    if (conn->method_complete && conn->path_complete && conn->headers_complete && conn->body_complete) {
      break;
    }

    // clear line buffer
    memset(linebuf, 0, sizeof(linebuf));

    // TODO: add timeout
    int len = recv(conn->fd, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN, 0);
    if (len < 0) {

      if (errno == EINTR) {
        continue;
      }

      __isere->logger->error(ISERE_HTTPD_LOG_TAG, "recv() error: %s", strerror(errno));
      return -1;
    }

    if (len == 0) {
      return 0;
    }

    enum llhttp_errno err = llhttp_execute(&conn->llhttp, linebuf, len);
    if (err != HPE_OK) {
      __isere->logger->error(ISERE_HTTPD_LOG_TAG, "llhttp_execute() error: %s %s", llhttp_errno_name(err), conn->llhttp.reason);
      return -1;
    }
  }

  return 0;
}

static isere_httpd_conn_t *__httpd_get_free_slot()
{
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    if (__conns[i].fd == -1) {
      return &__conns[i];
    }
  }

  return NULL;
}

void httpd_task(void *params)
{
  httpd_task_params_t *task_params = (httpd_task_params_t *)params;
  isere_httpd_t *httpd = task_params->httpd;
  httpd_handler_t *handler = task_params->handler;

  httpd->server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (httpd->server_fd < 0) {
    __isere->logger->error(ISERE_HTTPD_LOG_TAG, "socket() error: %s", strerror(errno));
    vTaskDelete(NULL);
    return;
  }

  struct sockaddr_in dest_addr;
  bzero(&dest_addr, sizeof(dest_addr));
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_port = htons(ISERE_HTTPD_PORT);

  int err = bind(httpd->server_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    __isere->logger->error(ISERE_HTTPD_LOG_TAG, "bind() error: %s", strerror(errno));
    goto cleanup;
  }

  err = listen(httpd->server_fd, 1);
  if (err != 0) {
    __isere->logger->error(ISERE_HTTPD_LOG_TAG, "listen() error: %s", strerror(errno));
    goto cleanup;
  }

  __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d", ISERE_HTTPD_PORT);

  for (;;) {

    isere_httpd_conn_t *conn = __httpd_get_free_slot();
    if (!conn) {
      vTaskDelay(50 / portTICK_PERIOD_MS);
      continue;
    }

    // accept connection
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);

    conn->fd = accept(httpd->server_fd, (struct sockaddr *)&source_addr, &addr_len);
    if (conn->fd < 0 && errno == EINTR) {
      continue;
    }

    if (conn->fd < 0) {
      __isere->logger->error(ISERE_HTTPD_LOG_TAG, "accept() error: %s (%d)", strerror(errno));
      continue;
    }

    // disable tcp keepalive
    int keep_alive = 0;
    setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));

    // initialize llhttp
    llhttp_settings_init(&conn->llhttp_settings);
    conn->llhttp_settings.on_method = __on_method;
    conn->llhttp_settings.on_url = __on_url;
    conn->llhttp_settings.on_url_complete = __on_url_complete;
    conn->llhttp_settings.on_method_complete = __on_method_complete;
    conn->llhttp_settings.on_header_field = __on_header_field;
    conn->llhttp_settings.on_header_value = __on_header_value;
    conn->llhttp_settings.on_header_field_complete = __on_header_field_complete;
    conn->llhttp_settings.on_header_value_complete = __on_header_value_complete;
    conn->llhttp_settings.on_headers_complete = __on_headers_complete;
    conn->llhttp_settings.on_body = __on_body;
    conn->llhttp_settings.on_message_complete = handle_on_message_complete;
    llhttp_init(&conn->llhttp, HTTP_REQUEST, &conn->llhttp_settings);
    conn->llhttp.data = (void *)conn;

    // convert ip address to string
    __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Received connection from %s", inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr));

    // read and parse http request
    int ret = __httpd_read_and_parse(conn);
    llhttp_finish(&conn->llhttp);

    if (ret < 0) {
      const char *buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
      write(conn->fd, buf, strlen(buf));
      goto cleanup_client;
    }

    uint32_t nbr_of_headers = MIN(conn->current_header_name_index, conn->current_header_value_index);
    handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

// cleanup client socket
cleanup_client:
    close(conn->fd);
    memset(conn, 0, sizeof(isere_httpd_conn_t));
    conn->fd = -1;
  }

cleanup:
  close(httpd->server_fd);
  httpd->server_fd = -1;

  vTaskDelete(NULL);
}
