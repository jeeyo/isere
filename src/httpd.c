#include "httpd.h"

#include <string.h>
#include <sys/param.h>

#include "tcp.h"
#include "js.h"

// #include "lwip/err.h"
// #include "lwip/errno.h"
// #include "lwip/sockets.h"
// #include "lwip/sys.h"
// #include "lwip/netdb.h"

static uint8_t should_exit = 0;

static isere_t *__isere = NULL;
static httpd_conn_t __conns[ISERE_HTTPD_MAX_CONNECTIONS];

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
  conn->completed |= METHOD_COMPLETED;
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
  conn->completed |= PATH_COMPLETED;
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
  conn->completed |= HEADERS_COMPLETED;
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
  conn->completed |= BODY_COMPLETED;
  return 0;
}

int httpd_init(isere_t *isere, isere_httpd_t *httpd)
{
  __isere = isere;

  if (isere->logger == NULL) {
    return -1;
  }

  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    memset(&__conns[i], 0, sizeof(httpd_conn_t));
    __conns[i].fd = -1;
    __conns[i].recvd = 0;
  }

  return 0;
}

int httpd_deinit(isere_httpd_t *httpd)
{
  if (__isere) {
    __isere = NULL;
  }

  // stop client tasks and close client sockets
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    httpd_conn_t *conn = &__conns[i];

    if (conn->tsk != NULL) {
      vTaskDelete(conn->tsk);
      conn->tsk = NULL;
    }

    if (__conns[i].fd != -1) {
      tcp_socket_close(conn->fd);
      memset(conn, 0, sizeof(httpd_conn_t));
      conn->fd = -1;
      conn->recvd = 0;
    }
  }

  tcp_socket_close(httpd->fd);

  // stop main httpd task
  should_exit = 1;

  return 0;
}

static int __httpd_process(httpd_conn_t *conn)
{
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  for(;;) {

    if (conn->completed == ALL_COMPLETED) {
      break;
    }

    int len = tcp_recv(conn->fd, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN);
    if (len == -2) {
      continue;
    }

    if (len < 0) {
      return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if (len == 0) {
      return 0;
    }

    enum llhttp_errno err = llhttp_execute(&conn->llhttp, linebuf, len);
    if (err != HPE_OK) {
      __isere->logger->error(ISERE_HTTPD_LOG_TAG, "__httpd_process() llhttp_execute() error: %s %s", llhttp_errno_name(err), conn->llhttp.reason);
      return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if ((conn->recvd + len) > ISERE_HTTPD_MAX_HTTP_REQUEST_LEN) {
      __isere->logger->warning(ISERE_HTTPD_LOG_TAG, "__httpd_process() error: request too long");
      return HTTP_STATUS_PAYLOAD_TOO_LARGE;
    }
  }

  return 0;
}

static httpd_conn_t *__httpd_get_free_slot()
{
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    if (__conns[i].fd == -1) {
      return &__conns[i];
    }
  }

  return NULL;
}

static void __httpd_cleanup_conn()
{
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    httpd_conn_t *conn = &__conns[i];

    if (__conns[i].fd == -1) {
      continue;
    }

    // cleanup finished task
    // TODO: also cleanup timed out task
    if (conn->tsk != NULL && eTaskGetState(conn->tsk) == eSuspended) {
      // cleanup client socket
      tcp_socket_close(conn->fd);
      memset(conn, 0, sizeof(httpd_conn_t));
      conn->fd = -1;
      conn->recvd = 0;

      vTaskDelete(conn->tsk);
      conn->tsk = NULL;
    }
  }
}

void httpd_client_handler_task(void *params)
{
  httpd_client_task_params_t *task_params = (httpd_client_task_params_t *)params;
  httpd_conn_t *conn = task_params->conn;
  httpd_handler_t *http_handler = task_params->handler;

  // read and parse http request
  int ret = __httpd_process(conn);
  llhttp_finish(&conn->llhttp);

  if (ret < 0) {
    const char *buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    tcp_write(conn->fd, buf, strlen(buf));
    vTaskSuspend(NULL);
  }

  uint32_t nbr_of_headers = MIN(conn->num_header_fields, conn->num_header_values);
  http_handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

  // suspend task once http handler done, wait for the main loop to delete it
  vTaskSuspend(NULL);
}

void httpd_task(void *params)
{
  httpd_task_params_t *task_params = (httpd_task_params_t *)params;
  isere_httpd_t *httpd = task_params->httpd;
  httpd_handler_t *handler = task_params->handler;

  httpd->fd = tcp_socket_new();
  if (httpd->fd < 0) {
    goto exit;
  }

  if (tcp_listen(httpd->fd, ISERE_HTTPD_PORT) < 0) {
    goto exit;
  }

  __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d", ISERE_HTTPD_PORT);

  for (;;) {

    __httpd_cleanup_conn();

    httpd_conn_t *conn = __httpd_get_free_slot();
    if (!conn) {
      vTaskDelay(50 / portTICK_PERIOD_MS);
      continue;
    }

    // accept connection
    char ipaddr[16];
    conn->fd = tcp_accept(httpd->fd, ipaddr);
    if (conn->fd < 0) {
      conn->fd = -1;
      continue;
    }

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

    // convert ip address to string
    __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Received connection from %s", ipaddr);

    // read and parse http request
    int ret = __httpd_process(conn);
    llhttp_finish(&conn->llhttp);

    if (ret < 0) {
      const char *buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
      tcp_write(conn->fd, buf, strlen(buf));
      goto client_cleanup;
    }

    uint32_t nbr_of_headers = MIN(conn->num_header_fields, conn->num_header_values);
    handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

    // httpd_client_task_params_t client_task_params;
    // client_task_params.conn = conn;
    // client_task_params.handler = handler;

    // // TODO: configurable stack size
    // if (xTaskCreate(httpd_client_handler_task, "httpd_client_handler", ISERE_JS_STACK_SIZE, (void *)&client_task_params, tskIDLE_PRIORITY + 1, &conn->tsk) != pdPASS) {
    //   __isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd task");
    // }

client_cleanup:
    tcp_socket_close(conn->fd);
    memset(conn, 0, sizeof(httpd_conn_t));
    conn->fd = -1;
    conn->recvd = 0;
  }

exit:
  vTaskDelete(NULL);
}
