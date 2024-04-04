#include "httpd.h"

#include "queue.h"

#include <string.h>
#include <sys/param.h>

#include "tcp.h"
#include "js.h"

static uint8_t should_exit = 0;

static isere_t *__isere = NULL;
static httpd_handler_t *__httpd_handler = NULL;

static TaskHandle_t __httpd_server_task_handle;
static TaskHandle_t __httpd_process_task_handle;
static xQueueHandle __httpd_conn_queue;

static httpd_conn_t __conns[ISERE_HTTPD_MAX_CONNECTIONS];

static void __isere_httpd_server_task(void *params);
static void __isere_httpd_process_task(void *params);

static void __httpd_cleanup_conn(httpd_conn_t *conn);

static int __on_method(llhttp_t *parser, const char *at, size_t length)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  // boundary check
  size_t len = strlen(conn->method);
  size_t available = ISERE_HTTPD_MAX_HTTP_METHOD_LEN - len - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->method + len, at, length);
  return 0;
}

static int __on_method_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;
  conn->completed |= METHODED;
  return 0;
}

static int __on_url(llhttp_t *parser, const char *at, size_t length)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  // boundary check
  size_t len = strlen(conn->path);
  size_t available = ISERE_HTTPD_MAX_HTTP_PATH_LEN - len - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->path + len, at, length);
  return 0;
}

static int __on_url_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;
  yuarel_parse(&conn->url_parser, conn->path);
  conn->completed |= PATHED;
  return 0;
}

static int __on_header_field(llhttp_t *parser, const char *at, size_t length)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  if (conn->num_header_fields < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t len = strlen(conn->headers[conn->num_header_fields].name);
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN - len - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(conn->headers[conn->num_header_fields].name + len, at, length);
  }

  return 0;
}

static int __on_header_field_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  if (conn->num_header_fields < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t len = strlen(conn->headers[conn->num_header_fields].name);
    conn->headers[conn->num_header_fields].name[len] = '\0';
    conn->num_header_fields++;
  }

  return 0;
}

static int __on_header_value(llhttp_t *parser, const char *at, size_t length)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  if (conn->num_header_values < ISERE_HTTPD_MAX_HTTP_HEADERS) {

    // boundary check
    size_t len = strlen(conn->headers[conn->num_header_values].value);
    size_t available = ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN - len - 1;

    if (length > available) {
      length = available;
    }

    if (length == 0) {
      return 0;
    }

    strncpy(conn->headers[conn->num_header_values].value + len, at, length);
  }

  return 0;
}

static int __on_header_value_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  if (conn->num_header_values < ISERE_HTTPD_MAX_HTTP_HEADERS) {
    size_t len = strlen(conn->headers[conn->num_header_values].value);
    conn->headers[conn->num_header_values].value[len] = '\0';
    conn->num_header_values++;
  }

  return 0;
}

static int __on_headers_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;
  conn->completed |= HEADERED;
  return 0;
}

static int __on_body(llhttp_t *parser, const char *at, size_t length)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;

  // boundary check
  size_t len = strlen(conn->body);
  size_t available = ISERE_HTTPD_MAX_HTTP_BODY_LEN - len - 1;

  if (length > available) {
    length = available;
  }

  if (length == 0) {
    return 0;
  }

  strncpy(conn->body + len, at, length);
  return 0;
}

static int __on_message_complete(llhttp_t *parser)
{
  httpd_conn_t *conn = (httpd_conn_t *)parser->data;
  conn->completed |= BODYED;
  return 0;
}

int isere_httpd_init(isere_t *isere, isere_httpd_t *httpd, httpd_handler_t *handler)
{
  __isere = isere;
  __httpd_handler = handler;

  if (isere->logger == NULL) {
    return -1;
  }

  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    memset(&__conns[i], 0, sizeof(httpd_conn_t));
    __httpd_cleanup_conn(&__conns[i]);
  }

  __httpd_conn_queue = xQueueCreate(ISERE_HTTPD_MAX_CONNECTIONS * 2, sizeof(httpd_conn_t *));
  if (__httpd_conn_queue == NULL) {
    __isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd queue");
    return -1;
  }

  // start web server task
  if (xTaskCreate(__isere_httpd_server_task, "httpd_server", configMINIMAL_STACK_SIZE, (void *)httpd, tskIDLE_PRIORITY + 2, &__httpd_server_task_handle) != pdPASS) {
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd server task");
    return -1;
  }

  // start connection processor task
  if (xTaskCreate(__isere_httpd_process_task, "httpd_process", configMINIMAL_STACK_SIZE, (void *)httpd, tskIDLE_PRIORITY + 2, &__httpd_process_task_handle) != pdPASS) {
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd processor task");
    return -1;
  }

  return 0;
}

int isere_httpd_deinit(isere_httpd_t *httpd)
{
  if (__isere) {
    __isere = NULL;
  }

  // stop client tasks and close client sockets
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    httpd_conn_t *conn = &__conns[i];
    __httpd_cleanup_conn(conn);
  }

  isere_tcp_socket_close(httpd->fd);

  // stop httpd tasks
  should_exit = 1;

  return 0;
}

static httpd_conn_t *__httpd_get_free_slot()
{
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    if (__conns[i].fd == PLATFORM_SOCKET_INVALID) {
      return &__conns[i];
    }
  }

  return NULL;
}

static void __httpd_cleanup_conn(httpd_conn_t *conn)
{
  if (conn->fd == PLATFORM_SOCKET_INVALID) {
    return;
  }

  llhttp_reset(&conn->llhttp);

  // cleanup client socket
  isere_tcp_socket_close(conn->fd);
  memset(conn, 0, sizeof(httpd_conn_t));
  conn->fd = PLATFORM_SOCKET_INVALID;
  conn->recvd = 0;
}

static void __isere_httpd_process_task(void *param)
{
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  while (!should_exit)
  {
    httpd_conn_t *conn = NULL;
    if (xQueueReceive(__httpd_conn_queue, &conn, portMAX_DELAY) != pdPASS) {
      goto finally;
    }

    if (conn == NULL) {
      continue;
    }

    int len = isere_tcp_recv(conn->fd, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN);
    if (len == -2) {
      if (xQueueSend(__httpd_conn_queue, &conn, pdMS_TO_TICKS(50)) != pdPASS) {
        goto finally;
      }

      continue;
    }

    if (len <= 0) {
      goto finally;
    }

    enum llhttp_errno err = llhttp_execute(&conn->llhttp, linebuf, len);
    if (err != HPE_OK) {
      __isere->logger->error(ISERE_HTTPD_LOG_TAG, "llhttp_execute() error: %s %s", llhttp_errno_name(err), conn->llhttp.reason);
      goto finally;
    }

    if ((conn->recvd + len) > ISERE_HTTPD_MAX_HTTP_REQUEST_LEN) {
      __isere->logger->warning(ISERE_HTTPD_LOG_TAG, "request too long");
      goto finally;
    }

    if (conn->completed != DONE) {
      if (xQueueSend(__httpd_conn_queue, &conn, pdMS_TO_TICKS(50)) != pdPASS) {
        goto finally;
      }

      continue;
    }

    if (__httpd_handler == NULL) {
      const char *buf = "HTTP/1.1 200 OK\r\n\r\n";
      isere_tcp_write(conn->fd, buf, strlen(buf));
      goto finally;
    }

    uint32_t nbr_of_headers = MIN(conn->num_header_fields, conn->num_header_values);
    __httpd_handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

finally:
    __httpd_cleanup_conn(conn);
  }

  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd process task was unexpectedly closed");
  should_exit = 1;
  vTaskDelete(NULL);
}

static void __isere_httpd_server_task(void *params)
{
  isere_httpd_t *httpd = (isere_httpd_t *)params;

  httpd->fd = isere_tcp_socket_new();
  if (httpd->fd == PLATFORM_SOCKET_INVALID) {
    goto exit;
  }

  if (isere_tcp_listen(httpd->fd, ISERE_HTTPD_PORT) < 0) {
    goto exit;
  }

  __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d", ISERE_HTTPD_PORT);

  while (!should_exit)
  {
    taskYIELD();

    // accept connection
    char ipaddr[16];
    platform_socket_t fd = isere_tcp_accept(httpd->fd, ipaddr);
    if (fd == PLATFORM_SOCKET_INVALID) {
      continue;
    }

    // __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Received connection from %s", ipaddr);

    httpd_conn_t *conn = __httpd_get_free_slot();
    if (conn == NULL) {
      isere_tcp_socket_close(fd);
      continue;
    }

    conn->fd = fd;

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
    conn->llhttp_settings.on_message_complete = __on_message_complete;
    llhttp_init(&conn->llhttp, HTTP_REQUEST, &conn->llhttp_settings);
    conn->llhttp.data = (void *)conn;

    if (xQueueSend(__httpd_conn_queue, &conn, pdMS_TO_TICKS(50)) != pdPASS) {
      __httpd_cleanup_conn(conn);
    }
  }

exit:
  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd server task was unexpectedly closed");
  should_exit = 1;
  vTaskDelete(NULL);
}
