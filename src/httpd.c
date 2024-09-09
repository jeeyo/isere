#include "httpd.h"

#include <string.h>
#include <sys/param.h>

#include "libuv/uv.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

static uint8_t should_exit = 0;

static isere_t *__isere = NULL;
static isere_httpd_t *__httpd = NULL;
static httpd_handler_t *__httpd_handler = NULL;

static void __httpd_task(void *params);

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

  uv__io_stop(&__httpd->loop, &conn->w, UV_POLLIN);

  // execute request handler function
  // if the request was done parsing
  // TODO: HTTP path
  if ((conn->path[0] != '\0') ||
    (conn->path[0] != '/' && conn->path[1] != '\0'))
  {
    const char *buf = "HTTP/1.1 404 Not Found\r\n\r\n";
    isere_tcp_write(conn->fd, buf, strlen(buf));
    __httpd_cleanup_conn(conn);
    return 0;
  }

  if (__httpd_handler == NULL) {
    const char *buf = "HTTP/1.1 200 OK\r\n\r\n";
    isere_tcp_write(conn->fd, buf, strlen(buf));
    __httpd_cleanup_conn(conn);
    return 0;
  }

  uint32_t nbr_of_headers = MIN(conn->num_header_fields, conn->num_header_values);
  __httpd_handler(__isere, conn, conn->method, conn->url_parser.path, conn->url_parser.query, conn->headers, nbr_of_headers, conn->body);

  uv__queue_insert_tail(&__httpd->js_queue, &conn->js_queue);
  return 0;
}

static void __on_poll(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  if ((events & UV_POLLIN) != UV_POLLIN) {
    return;
  }

  httpd_conn_t *conn = (httpd_conn_t *)w->opaque;

  char linebuf[ISERE_HTTPD_LINE_BUFFER_LEN];

  // if the http header parsing is not done
  // and there's a new incoming data
  int len = isere_tcp_recv(conn->fd, linebuf, ISERE_HTTPD_LINE_BUFFER_LEN);
  if (len <= 0) {
    if (len == -2) {  // EAGAIN
      return;
    }

    __httpd_cleanup_conn(conn);
    return;
  }

  if ((conn->recvd + len) > ISERE_HTTPD_MAX_HTTP_REQUEST_LEN) {
    __isere->logger->warning(ISERE_HTTPD_LOG_TAG, "Request too large: Got %d bytes", conn->recvd + len);

    const char *buf = "HTTP/1.1 413 Content Too Large\r\n\r\n";
    isere_tcp_write(conn->fd, buf, strlen(buf));
    __httpd_cleanup_conn(conn);
    return;
  }

  // TODO: make this async
  // pass the new data to http parser
  enum llhttp_errno err = llhttp_execute(&conn->llhttp, linebuf, len);
  if (err != HPE_OK) {
    __isere->logger->error(ISERE_HTTPD_LOG_TAG, "llhttp_execute() error: %s %s", llhttp_errno_name(err), conn->llhttp.reason);
    __httpd_cleanup_conn(conn);
    return;
  }
}

static void __on_connected(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  if ((events & UV_POLLIN) != UV_POLLIN) {
    return;
  }

  // accept connection
  char ipaddr[16];
  int newfd = isere_tcp_accept(__httpd->serverfd, ipaddr);
  if (newfd < 0) {
    return;
  }

  // __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Received connection from %s", ipaddr);

  httpd_conn_t *conn = (httpd_conn_t *)pvPortMalloc(sizeof(httpd_conn_t));
  if (conn == NULL) {
    isere_tcp_close(newfd);
    return;
  }

  memset(conn, 0, sizeof(httpd_conn_t));

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
    isere_js_new_context(__isere->js, &conn->js);
    uv__queue_init(&conn->js_queue);
  }

  conn->fd = newfd;
  conn->recvd = 0;

  uv__io_t *sw = &conn->w;
  sw->fd = newfd;
  sw->events = 0;
  sw->cb = __on_poll;
  sw->opaque = (void *)conn;
  uv__queue_init(&sw->watcher_queue);

  uv__io_start(&__httpd->loop, sw, UV_POLLIN);
}

int isere_httpd_init(isere_t *isere, isere_httpd_t *httpd, httpd_handler_t *handler)
{
  __isere = isere;
  __httpd = httpd;
  __httpd_handler = handler;

  if (isere->logger == NULL) {
    return -1;
  }

  uv__platform_loop_init(&httpd->loop);
  httpd->loop.nfds = 0;
  httpd->loop.watchers = NULL;
  httpd->loop.nwatchers = 0;
  uv__queue_init(&httpd->loop.watcher_queue);
  uv__queue_init(&httpd->js_queue);

  // start httpd task
  if (xTaskCreate(__httpd_task, "httpd", ISERE_HTTPD_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &httpd->tsk) != pdPASS) {
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

  if (__httpd) {
    __httpd = NULL;
  }

  if (__httpd_handler) {
    __httpd_handler = NULL;
  }

  uv__platform_loop_delete(&httpd->loop);

  isere_tcp_close(httpd->serverfd);

  // stop httpd tasks
  should_exit = 1;

  return 0;
}

static void __httpd_cleanup_conn(httpd_conn_t *conn)
{
  llhttp_reset(&conn->llhttp);
  isere_js_free_context(&conn->js);

  if (__httpd_handler != NULL) {
    uv__queue_remove(&conn->js_queue);
  }

  // cleanup client socket
  uv__io_stop(&__httpd->loop, &conn->w, UV_POLLIN);
  isere_tcp_close(conn->fd);
  vPortFree(conn);
}

static void __httpd_task(void *param)
{
  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  if ((__httpd->serverfd = isere_tcp_socket_new()) < 0) {
    goto exit;
  }

  if (isere_tcp_listen(__httpd->serverfd, ISERE_HTTPD_PORT) < 0) {
    goto exit;
  }

  uv__io_t *w = &__httpd->w;
  w->fd = __httpd->serverfd;
  w->events = 0;
  w->cb = __on_connected;
  w->opaque = NULL;
  uv__queue_init(&w->watcher_queue);

  uv__io_start(&__httpd->loop, w, UV_POLLIN);

  __isere->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d", ISERE_HTTPD_PORT);

  while (!should_exit)
  {
    // poll sockets to see if there's any new events
    uv__io_poll(&__httpd->loop, 0);

    if (__httpd_handler == NULL) {
      continue;
    }

    while (!uv__queue_empty(&__httpd->js_queue))
    {
      struct uv__queue *q = uv__queue_head(&__httpd->js_queue);
      uv__queue_remove(q);
      uv__queue_init(q);

      httpd_conn_t *conn = uv__queue_data(q, httpd_conn_t, js_queue);

      // ignore empty slots
      if (conn->fd == -1) {
        continue;
      }

      // if http handler function was invoked
      // and pending jobs are not done yet
      int is_done = 0;

      // respond as the response object is constructed from `cb()`
      JSValue global_obj = JS_GetGlobalObject(conn->js.context);
      JSValue response_obj = JS_GetPropertyStr(conn->js.context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);
      if (!JS_IsUndefined(response_obj)) {
        // TODO: callbackWaitsForEmptyEventLoop
        is_done = 1;
      }
      JS_FreeValue(conn->js.context, response_obj);
      JS_FreeValue(conn->js.context, global_obj);

      // add back to queue if there are pending jobs left
      if (!is_done && isere_js_poll(&conn->js) != 0) {
        uv__queue_insert_tail(&__httpd->js_queue, &conn->js_queue);
        continue;
      }

writeback:
      // TODO: non-blocking write?
      __httpd_writeback(conn);
      __httpd_cleanup_conn(conn);
    }
  }

exit:
  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd poller task was unexpectedly closed");
  uv__io_stop(&__httpd->loop, &__httpd->w, UV_POLLIN);
  should_exit = 1;
  vTaskDelete(NULL);
}

static void __httpd_writeback(httpd_conn_t *conn)
{
  isere_js_context_t *ctx = &conn->js;

  JS_FreeValue(ctx->context, ctx->future);

  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  JSValue response_obj = JS_GetPropertyStr(ctx->context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);

  // send HTTP response code
  isere_tcp_write(conn->fd, "HTTP/1.1 ", 9);
  JSValue statusCode = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME);
  if (!JS_IsNumber(statusCode)) {
    isere_tcp_write(conn->fd, "200 OK", 3);
  } else {
    size_t len = 0;
    const char *statusCode1 = JS_ToCStringLen(ctx->context, &len, statusCode);
    isere_tcp_write(conn->fd, statusCode1, len);

    // TODO: status code to status text

    JS_FreeCString(ctx->context, statusCode1);
  }
  isere_tcp_write(conn->fd, "\r\n", 2);
  JS_FreeValue(ctx->context, statusCode);

  // send HTTP response headers
  JSValue headers = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_HEADERS_PROP_NAME);
  if (JS_IsObject(headers)) {

    JSPropertyEnum *props = NULL;
    uint32_t props_len = 0;

    if (JS_GetOwnPropertyNames(ctx->context, &props, &props_len, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

      for (int i = 0; i < props_len; i++) {

        const char *header_field_str = JS_AtomToCString(ctx->context, props[i].atom);

        JSValue header_value = JS_GetProperty(ctx->context, headers, props[i].atom);
        size_t header_value_len = 0;
        const char *header_value_str = JS_ToCStringLen(ctx->context, &header_value_len, header_value);

        isere_tcp_write(conn->fd, header_field_str, strlen(header_field_str));
        isere_tcp_write(conn->fd, ": ", 2);
        isere_tcp_write(conn->fd, header_value_str, header_value_len);
        isere_tcp_write(conn->fd, "\r\n", 2);

        JS_FreeCString(ctx->context, header_field_str);
        JS_FreeCString(ctx->context, header_value_str);
        JS_FreeValue(ctx->context, header_value);

        JS_FreeAtom(ctx->context, props[i].atom);
      }

      js_free(ctx->context, props);
    }
  }
  JS_FreeValue(ctx->context, headers);

  // TODO: `Date`
  const char *server_header = "Server: isere\r\n";
  isere_tcp_write(conn->fd, server_header, strlen(server_header));
  isere_tcp_write(conn->fd, "\r\n", 2);

  // send HTTP response body
  JSValue body = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_BODY_PROP_NAME);
  size_t body_len = 0;
  const char *body_str = NULL;

  if (JS_IsObject(body)) {
    JSValue stringifiedBody = JS_JSONStringify(ctx->context, body, JS_UNDEFINED, JS_UNDEFINED);
    body_str = JS_ToCStringLen(ctx->context, &body_len, stringifiedBody);
    JS_FreeValue(ctx->context, stringifiedBody);
  } else if (JS_IsString(body)) {
    body_str = JS_ToCStringLen(ctx->context, &body_len, body);
  }

  if (body_str != NULL) {
    isere_tcp_write(conn->fd, body_str, body_len);
    JS_FreeCString(ctx->context, body_str);
  }
  JS_FreeValue(ctx->context, body);

  JS_FreeValue(ctx->context, response_obj);
  JS_FreeValue(ctx->context, global_obj);

  isere_tcp_write(conn->fd, "\r\n\r\n", 4);
}
