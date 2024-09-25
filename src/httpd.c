#include "httpd.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "libuv/uv.h"

#include "FreeRTOS.h"
#include "task.h"

#include "runtime.h"

#include "queue.h"

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

  // stop receiving
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

  isere_js_new_context(__isere->js, &conn->js);
  conn->js.opaque = conn;
  uv__queue_init(&conn->js_queue);
  conn->js.initialized = 1;

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

  conn->fd = newfd;
  conn->recvd = 0;
  conn->response.completed = 0;
  conn->response.statusCode = 200;
  conn->response.num_header_fields = 0;
  conn->response.body = NULL;
  conn->response.body_len = 0;

  conn->js.initialized = 0;

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
    isere->logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd task");
    return -1;
  }

  return 0;
}

int isere_httpd_deinit(isere_httpd_t *httpd)
{
  if (__isere) {
    __isere->should_exit = 1;
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

  return 0;
}

static void __httpd_cleanup_conn(httpd_conn_t *conn)
{
  // llhttp_reset(&conn->llhttp);

  if (conn->js.initialized) {
    uv__queue_remove(&conn->js_queue);
    isere_js_free_context(__isere->js, &conn->js);
    conn->js.initialized = 0;
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

  while (!__isere->should_exit)
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

      // is the handler javascript function returned?
      // TODO: callbackWaitsForEmptyEventLoop
      int callbacked = conn->response.completed == 1;
      int pending_jobs_left = isere_js_poll(&conn->js);

      // add back to queue if there are pending jobs left
      if (!callbacked && pending_jobs_left != 0) {
        if (pending_jobs_left < 0) {
          goto fail;
        }

        uv__queue_insert_tail(&__httpd->js_queue, &conn->js_queue);
        continue;
      }

writeback:
      // TODO: non-blocking write?
      __httpd_writeback(conn);
fail:
      __httpd_cleanup_conn(conn);
    }
  }

exit:
  __isere->logger->error(ISERE_HTTPD_LOG_TAG, "httpd task was unexpectedly closed");
  uv__io_stop(&__httpd->loop, &__httpd->w, UV_POLLIN);
  __isere->should_exit = 1;
  vTaskDelete(NULL);
}

static void __httpd_writeback(httpd_conn_t *conn)
{
  httpd_response_object_t *resp = &conn->response;

  if (!resp->completed) {
    const char *buf = "HTTP/1.1 200 OK\r\n\r\n";
    isere_tcp_write(conn->fd, buf, strlen(buf));
    return;
  }

  // send HTTP response code
  isere_tcp_write(conn->fd, "HTTP/1.1 ", 9);

  char statusCode[4] = "200";
  int len = snprintf(statusCode, 4, "%d", resp->statusCode);
  isere_tcp_write(conn->fd, statusCode, len);
  // TODO: status code to status text
  isere_tcp_write(conn->fd, "\r\n", 2);

  // send HTTP response headers
  for (int i = 0; i < resp->num_header_fields; i++) {
    isere_tcp_write(conn->fd, resp->header_names[i], strlen(resp->header_names[i]));
    isere_tcp_write(conn->fd, ": ", 2);
    isere_tcp_write(conn->fd, resp->header_values[i], strlen(resp->header_values[i]));
    isere_tcp_write(conn->fd, "\r\n", 2);
  }

  // TODO: `Date`
  const char *server_header = "Server: isere\r\n";
  isere_tcp_write(conn->fd, server_header, strlen(server_header));
  isere_tcp_write(conn->fd, "\r\n", 2);

  // send HTTP response body
  isere_tcp_write(conn->fd, resp->body, resp->body_len);

  isere_tcp_write(conn->fd, "\r\n\r\n", 4);
}
