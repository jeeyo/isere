#include "httpd.h"

#include <string.h>
#include <sys/param.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tcp.h"

extern uint8_t should_exit;

static isere_t *__isere = NULL;
static httpd_handler_t *__httpd_handler = NULL;

static TaskHandle_t __httpd_server_task_handle = NULL;
static TaskHandle_t __httpd_process_task_handle = NULL;
static TaskHandle_t __httpd_poll_task_handle = NULL;

static tcp_socket_t __server_socket;
static httpd_conn_t __conns[ISERE_HTTPD_MAX_CONNECTIONS];

static void __httpd_server_task(void *params);
static void __httpd_parser_task(void *params);
static void __httpd_poller_task(void *params);

static void __httpd_cleanup_conn(httpd_conn_t *conn);
static void __httpd_writeback(httpd_conn_t *conn);

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
    httpd_conn_t *conn = &__conns[i];
    memset(conn, 0, sizeof(httpd_conn_t));
    conn->socket = NULL;
    conn->recvd = 0;
  }

  // start web server task
  if (xTaskCreate(__httpd_server_task, "httpd_server", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &__httpd_server_task_handle) != pdPASS) {
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd server task");
    return -1;
  }

  // start processor task
  if (xTaskCreate(__httpd_parser_task, "httpd_parser", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &__httpd_process_task_handle) != pdPASS) {
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd parser task");
    return -1;
  }

  // start poller task
  if (xTaskCreate(__httpd_poller_task, "httpd_poller", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &__httpd_poll_task_handle) != pdPASS) {
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd poller task");
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

  isere_tcp_socket_close(&__server_socket);

  // stop httpd tasks
  should_exit = 1;

  return 0;
}

static httpd_conn_t *__httpd_get_free_slot()
{
  for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++) {
    if (__conns[i].socket == NULL) {
      return &__conns[i];
    }
  }

  return NULL;
}

static void __httpd_cleanup_conn(httpd_conn_t *conn)
{
  llhttp_reset(&conn->llhttp);
  isere_js_deinit(&conn->js);

  // cleanup client socket
  isere_tcp_socket_close(conn->socket);
  memset(conn, 0, sizeof(httpd_conn_t));
  conn->socket = NULL;
  conn->recvd = 0;
  conn->completed = 0;
}

static void __httpd_poller_task(void *params)
{
  while (!should_exit)
  {
    taskYIELD();

    for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++)
    {
      httpd_conn_t *conn = &__conns[i];

      // skip if the connection processing is DONE
      if ((conn->completed & DONE) == DONE) {
        continue;
      }

      // or it is not waiting for JavaScript jobs
      if (!(conn->completed & POLLING)) {
        continue;
      }

      if (!(conn->completed & PROCESSED)) {

        if (isere_js_poll(&conn->js) == 0) {
          conn->completed |= PROCESSED;
          break;
        }

        JSValue global_obj = JS_GetGlobalObject(conn->js.context);
        JSValue response_obj = JS_GetPropertyStr(conn->js.context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);
        if (!JS_IsUndefined(response_obj)) {
          // TODO: callbackWaitsForEmptyEventLoop
          conn->completed |= PROCESSED;
        }
        JS_FreeValue(conn->js.context, response_obj);
        JS_FreeValue(conn->js.context, global_obj);
      }

      if ((conn->completed & PROCESSED) && !(conn->completed & WRITING)) {
        conn->completed |= WRITING;
        __httpd_writeback(conn);  // TODO: non-blocking write?
        break;
      }
    }
  }

  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd poller task was unexpectedly closed");
  should_exit = 1;
  vTaskDelete(NULL);
}

static void __httpd_parser_task(void *param)
{
  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  while (!should_exit)
  {
    taskYIELD();

    for (int i = 0; i < ISERE_HTTPD_MAX_CONNECTIONS; i++)
    {
      httpd_conn_t *conn = &__conns[i];

      // ignore empty slots
      if (conn->socket == NULL) {
        continue;
      }

      // disconnect processed connections
      if ((conn->completed & DONE) == DONE) {
        goto finally;
      }

      // ignore parsed connections
      if (conn->completed >= PARSED) {
        continue;
      }

      // skip if there's no new events
      // or an error occurred
      if (isere_tcp_poll(conn->socket, 0) <= 0) {
        continue;
      }

      // ignore connections with no incoming data
      if (!(conn->socket->revents & TCP_POLL_READ_READY)) {
        continue;
      }

      int len = isere_tcp_recv(conn->socket, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN);
      if (len <= 0) {
        if (len == -2) {  // EAGAIN
          continue;
        }
        goto finally;
      }

      enum llhttp_errno err = llhttp_execute(&conn->llhttp, linebuf, len);
      if (err != HPE_OK) {
        __isere->logger->error(ISERE_HTTPD_LOG_TAG, "llhttp_execute() error: %s %s", llhttp_errno_name(err), conn->llhttp.reason);
        goto finally;
      }

      if ((conn->recvd + len) > ISERE_HTTPD_MAX_HTTP_REQUEST_LEN) {
        __isere->logger->warning(ISERE_HTTPD_LOG_TAG, "request too long");

        const char *buf = "HTTP/1.1 413 Content Too Large\r\n\r\n";
        isere_tcp_write(conn->socket, buf, strlen(buf));
        goto finally;
      }

      if (conn->completed < PARSED) {
        continue;
      }

      if (__httpd_handler == NULL) {
        const char *buf = "HTTP/1.1 200 OK\r\n\r\n";
        isere_tcp_write(conn->socket, buf, strlen(buf));
        goto finally;
      }

      uint32_t nbr_of_headers = MIN(conn->num_header_fields, conn->num_header_values);
      __httpd_handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

      conn->completed |= POLLING;
      continue;

  finally:
      __httpd_cleanup_conn(conn);
    }
  }

  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd process task was unexpectedly closed");
  should_exit = 1;
  vTaskDelete(NULL);
}

static void __httpd_server_task(void *params)
{
  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  if (isere_tcp_socket_init(&__server_socket) < 0) {
    goto exit;
  }

  if (isere_tcp_listen(&__server_socket, ISERE_HTTPD_PORT) < 0) {
    goto exit;
  }

  __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d", ISERE_HTTPD_PORT);

  while (!should_exit)
  {
    // accept connection
    char ipaddr[16];
    tcp_socket_t *newsock = isere_tcp_accept(&__server_socket, ipaddr);
    if (newsock == NULL) {
      continue;
    }

    // __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Received connection from %s", ipaddr);

    httpd_conn_t *conn = __httpd_get_free_slot();
    if (conn == NULL) {
      isere_tcp_socket_close(newsock);
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

    if (__httpd_handler != NULL) {
      isere_js_init(&conn->js);
    }

    conn->socket = newsock;
  }

exit:
  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd server task was unexpectedly closed");
  should_exit = 1;
  vTaskDelete(NULL);
}

static void __httpd_writeback(httpd_conn_t *conn)
{
  isere_js_t *js = &conn->js;

  JS_FreeValue(js->context, js->future);

  JSValue global_obj = JS_GetGlobalObject(js->context);
  JSValue response_obj = JS_GetPropertyStr(js->context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);

  // send HTTP response code
  isere_tcp_write(conn->socket, "HTTP/1.1 ", 9);
  JSValue statusCode = JS_GetPropertyStr(js->context, response_obj, ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME);
  if (!JS_IsNumber(statusCode)) {
    isere_tcp_write(conn->socket, "200 OK", 3);
  } else {
    size_t len = 0;
    const char *statusCode1 = JS_ToCStringLen(js->context, &len, statusCode);
    isere_tcp_write(conn->socket, statusCode1, len);

    // TODO: status code to status text

    JS_FreeCString(js->context, statusCode1);
  }
  isere_tcp_write(conn->socket, "\r\n", 2);
  JS_FreeValue(js->context, statusCode);

  // send HTTP response headers
  JSValue headers = JS_GetPropertyStr(js->context, response_obj, ISERE_JS_RESPONSE_HEADERS_PROP_NAME);
  if (JS_IsObject(headers)) {

    JSPropertyEnum *props = NULL;
    uint32_t props_len = 0;

    if (JS_GetOwnPropertyNames(js->context, &props, &props_len, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

      for (int i = 0; i < props_len; i++) {

        const char *header_field_str = JS_AtomToCString(js->context, props[i].atom);

        JSValue header_value = JS_GetProperty(js->context, headers, props[i].atom);
        size_t header_value_len = 0;
        const char *header_value_str = JS_ToCStringLen(js->context, &header_value_len, header_value);

        isere_tcp_write(conn->socket, header_field_str, strlen(header_field_str));
        isere_tcp_write(conn->socket, ": ", 2);
        isere_tcp_write(conn->socket, header_value_str, header_value_len);
        isere_tcp_write(conn->socket, "\r\n", 2);

        JS_FreeCString(js->context, header_field_str);
        JS_FreeCString(js->context, header_value_str);
        JS_FreeValue(js->context, header_value);

        JS_FreeAtom(js->context, props[i].atom);
      }

      js_free(js->context, props);
    }
  }
  JS_FreeValue(js->context, headers);

  // TODO: `Date`
  const char *server_header = "Server: isere\r\n";
  isere_tcp_write(conn->socket, server_header, strlen(server_header));
  isere_tcp_write(conn->socket, "\r\n", 2);

  // send HTTP response body
  JSValue body = JS_GetPropertyStr(js->context, response_obj, ISERE_JS_RESPONSE_BODY_PROP_NAME);
  size_t body_len = 0;
  const char *body_str = NULL;

  if (JS_IsObject(body)) {
    JSValue stringifiedBody = JS_JSONStringify(js->context, body, JS_UNDEFINED, JS_UNDEFINED);
    body_str = JS_ToCStringLen(js->context, &body_len, stringifiedBody);
    JS_FreeValue(js->context, stringifiedBody);
  } else if (JS_IsString(body)) {
    body_str = JS_ToCStringLen(js->context, &body_len, body);
  }

  if (body_str != NULL) {
    isere_tcp_write(conn->socket, body_str, body_len);
    JS_FreeCString(js->context, body_str);
  }
  JS_FreeValue(js->context, body);

  JS_FreeValue(js->context, response_obj);
  JS_FreeValue(js->context, global_obj);

  isere_tcp_write(conn->socket, "\r\n\r\n", 4);

  conn->completed |= WROTE;
}
