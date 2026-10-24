#include "httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "engine.h"
#include "fetch.h"
#include "http_parser.h"
#include "loop.h"
#include "mem.h"
#include "metrics.h"
#include "platform.h"
#include "request.h"
#include "tcp.h"
#include "isere_config.h"

// Connection lifecycle:
//
//   READ    receive into conn->buf and re-parse the head from its start until a
//           whole request (head and body) is there
//   HANDLE  a built-in, a static file or a JS endpoint works on it; nothing more
//           is read meanwhile, so the spans into conn->buf stay valid
//   WRITE   the response in conn->out goes out, then the request's bytes are
//           dropped from the front of conn->buf and any pipelined request that
//           followed it is parsed from the same buffer
//
// A connection is never freed while a callback that uses it is on the stack:
// __conn_close() only marks it, and the outermost callback frees it.

typedef enum { CONN_READ = 0, CONN_HANDLE, CONN_WRITE } conn_state_t;

typedef struct {
  int fd;
  uv__io_t w;
  isere_timer_t timer;
  struct uv__queue node;
  conn_state_t state;
  int self;               // opened by this server's own fetch()
  int depth;              // callbacks on the stack that use this connection
  int dead;               // free it once depth drops to 0
  int processing;         // inside __process(), which picks up pipelined requests itself
  int head_timer;         // the head timeout is running for the current request
  int sent_continue;

  char *buf;
  size_t cap;
  size_t len;
  http_header_t *headers;
  size_t headers_cap;

  // the request being handled
  http_request_head_t head;
  size_t head_len;
  http_framing_t framing;
  size_t consumed;        // bytes of buf this request occupies, body framing included
  int is_head;
  isere_request_t req;

  // its response
  char *out;
  size_t out_cap;
  size_t out_len;
  size_t out_sent;
  size_t body_len;
  int close_after;
  uint32_t served;
} httpd_conn_t;

static struct {
  isere_httpd_t *httpd;
  int serverfd;
  uv__io_t w;
  struct uv__queue conns;
  uint32_t active;        // connections from clients
  uint32_t active_self;   // connections this server opened to itself
  uint32_t max_conns;
  size_t head_budget;
  size_t body_budget;
  uint32_t next_request_id;
  isere_timer_t tick;
  const char **route_methods;
  const char **route_paths;
} __s;

static void __process(httpd_conn_t *conn);
static void __flush(httpd_conn_t *conn);

#define ENTER(conn) ((conn)->depth++)
#define LEAVE(conn) do { if (--(conn)->depth == 0 && (conn)->dead) __conn_free(conn); } while (0)

// ---------------------------------------------------------------------------
// connections

static void __conn_free(httpd_conn_t *conn)
{
  uv__io_stop(isere_loop(), &conn->w, UV_POLLIN | UV_POLLOUT);
  uv__platform_invalidate_fd(isere_loop(), conn->fd);
  isere_tcp_close(conn->fd);
  isere_timer_stop(&conn->timer);
  uv__queue_remove(&conn->node);

  if (conn->self) {
    __s.active_self--;
  } else {
    __s.active--;
  }
  isere_metrics_conn(ISERE_CONN_CLOSED);

  isere_free(conn->buf);
  isere_free(conn->headers);
  isere_free(conn->out);
  isere_free(conn);
}

static void __conn_close(httpd_conn_t *conn)
{
  if (conn->depth > 0) {
    conn->dead = 1;
    uv__io_stop(isere_loop(), &conn->w, UV_POLLIN | UV_POLLOUT);
    isere_timer_stop(&conn->timer);
    return;
  }
  __conn_free(conn);
}

// ---------------------------------------------------------------------------
// responses

static const char *__reason(int status)
{
  switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default: return "";
  }
}

static int __out_reserve(httpd_conn_t *conn, size_t extra)
{
  if (conn->out_len + extra <= conn->out_cap) {
    return 0;
  }
  size_t cap = conn->out_cap ? conn->out_cap : 256;
  while (cap < conn->out_len + extra) {
    cap *= 2;
  }
  char *out = isere_realloc(conn->out, cap);
  if (out == NULL) {
    return -1;
  }
  conn->out = out;
  conn->out_cap = cap;
  return 0;
}

static void __out_append(httpd_conn_t *conn, const char *data, size_t len)
{
  if (len == 0 || __out_reserve(conn, len) < 0) {
    return;
  }
  memcpy(conn->out + conn->out_len, data, len);
  conn->out_len += len;
}

static void __out_append_uint(httpd_conn_t *conn, uint64_t n)
{
  char digits[24];
  int i = sizeof(digits);
  do {
    digits[--i] = (char)('0' + n % 10);
    n /= 10;
  } while (n > 0);
  __out_append(conn, digits + i, sizeof(digits) - (size_t)i);
}

static void __out_append_literal(httpd_conn_t *conn, const char *s)
{
  size_t n = 0;
  while (s[n] != '\0') n++;
  __out_append(conn, s, n);
}

void isere_response_start(isere_request_t *req, int status)
{
  httpd_conn_t *conn = (httpd_conn_t *)req->conn;
  if (req->responded) {
    return;
  }
  if (status < 100 || status > 599) {
    status = 500;
  }
  req->status = status;
  conn->out_len = 0;
  conn->out_sent = 0;

  __out_append_literal(conn, "HTTP/1.1 ");
  __out_append_uint(conn, (uint64_t)status);
  __out_append_literal(conn, " ");
  __out_append_literal(conn, __reason(status));
  __out_append_literal(conn, "\r\n");
}

static int __is_token(const char *s, size_t len)
{
  if (len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c <= 0x20 || c >= 0x7f || c == ':' || c == '(' || c == ')' || c == ',' || c == '/' ||
        c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '@' || c == '[' ||
        c == '\\' || c == ']' || c == '{' || c == '}' || c == '"') {
      return 0;
    }
  }
  return 1;
}

static int __is_field_value(const char *s, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\r' || c == '\n' || c == '\0' || c == 0x7f || (c < 0x20 && c != '\t')) {
      return 0;
    }
  }
  return 1;
}

void isere_response_header(isere_request_t *req, const char *name, size_t name_len,
  const char *value, size_t value_len)
{
  httpd_conn_t *conn = (httpd_conn_t *)req->conn;
  if (req->responded || !__is_token(name, name_len) || !__is_field_value(value, value_len)) {
    return;
  }

  // isère frames the message itself
  http_span_t span = { 0, (uint32_t)name_len };
  if (http_span_equals(name, span, "content-length") || http_span_equals(name, span, "transfer-encoding") ||
      http_span_equals(name, span, "connection") || http_span_equals(name, span, "server-timing")) {
    return;
  }

  __out_append(conn, name, name_len);
  __out_append_literal(conn, ": ");
  __out_append(conn, value, value_len);
  __out_append_literal(conn, "\r\n");
}

char *isere_response_body(isere_request_t *req, size_t body_len)
{
  httpd_conn_t *conn = (httpd_conn_t *)req->conn;
  if (req->responded) {
    return NULL;
  }

  isere_request_metrics_t *m = &req->metrics;
  if (isere_metrics_current() == m) {
    // close the running stretch so its CPU time is in the header
    isere_metrics_stretch_end();
    isere_metrics_stretch_begin(m);
  }
  m->wall_us = platform_now_us() - m->start_us;
  // what this connection holds for the request: receive buffer, spans, response
  m->conn_bytes = (uint32_t)(conn->cap + conn->headers_cap * sizeof(http_header_t) + conn->out_len + 256 + body_len);

  __out_append_literal(conn, "Content-Length: ");
  __out_append_uint(conn, body_len);
  __out_append_literal(conn, "\r\n");

  if (isere_metrics_enabled()) {
    char timing[192];
    int n = isere_metrics_server_timing(m, timing, sizeof(timing));
    if (n > 0) {
      __out_append_literal(conn, "Server-Timing: ");
      __out_append(conn, timing, (size_t)n);
      __out_append_literal(conn, "\r\n");
    }
  }

  int keep_alive = conn->framing.keep_alive && !conn->close_after && !__s.httpd->should_exit;
  conn->close_after = !keep_alive;
  __out_append_literal(conn, keep_alive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n");

  if (__out_reserve(conn, body_len + 1) < 0) {
    return NULL;
  }
  char *body = conn->out + conn->out_len;
  conn->out_len += body_len;
  conn->body_len = body_len;
  return body;
}

void isere_response_end(isere_request_t *req)
{
  httpd_conn_t *conn = (httpd_conn_t *)req->conn;
  if (req->responded) {
    return;
  }
  if (conn->is_head) {
    // HEAD gets the GET response's headers without its body
    conn->out_len -= conn->body_len;
  }
  req->responded = 1;
}

void isere_response_finish(isere_request_t *req, const char *body, size_t body_len)
{
  char *p = isere_response_body(req, body_len);
  if (p == NULL) {
    if (!req->responded) {
      // out of memory for the body: send what fits, the headers say how long it should be
      isere_response_end(req);
      ((httpd_conn_t *)req->conn)->close_after = 1;
    }
    return;
  }
  if (body_len > 0) {
    memcpy(p, body, body_len);
  }
  isere_response_end(req);
}

void isere_response_simple(isere_request_t *req, int status, const char *content_type,
  const char *body, size_t body_len)
{
  isere_response_start(req, status);
  if (content_type != NULL) {
    size_t n = 0;
    while (content_type[n] != '\0') n++;
    isere_response_header(req, "Content-Type", 12, content_type, n);
  }
  isere_response_finish(req, body, body_len);
}

void isere_request_complete(isere_request_t *req)
{
  httpd_conn_t *conn = (httpd_conn_t *)req->conn;
  ENTER(conn);

  if (!req->responded) {
    static const char msg[] = "no response\n";
    isere_response_simple(req, 500, "text/plain", msg, sizeof(msg) - 1);
  }

  isere_metrics_record(req->route, req->status, &req->metrics);
  isere_metrics_mark_first_request();

  conn->state = CONN_WRITE;
  if (!conn->dead) {
    __flush(conn);
  }
  LEAVE(conn);
}

// ---------------------------------------------------------------------------
// requests

static void __reset_request(httpd_conn_t *conn)
{
  memset(&conn->head, 0, sizeof(conn->head));
  memset(&conn->framing, 0, sizeof(conn->framing));
  conn->head_len = 0;
  conn->consumed = 0;
  conn->is_head = 0;
  conn->head_timer = 0;
  conn->sent_continue = 0;
}

static void __begin_request(httpd_conn_t *conn, int route)
{
  isere_request_t *req = &conn->req;
  memset(req, 0, sizeof(*req));
  if (++__s.next_request_id == 0) {
    __s.next_request_id = 1;
  }
  req->id = __s.next_request_id;
  req->route = route;
  req->buf = conn->buf;
  req->head = &conn->head;
  req->headers = conn->headers;
  req->conn = conn;
  req->metrics.start_us = platform_now_us();
  conn->state = CONN_HANDLE;
  isere_timer_stop(&conn->timer);
  uv__io_stop(isere_loop(), &conn->w, UV_POLLIN);
}

// answers a request that can't be handled (400, 413, 431, 408) and closes the connection
static void __reject(httpd_conn_t *conn, int status)
{
  static const char msg[] = "\n";
  __begin_request(conn, ISERE_METRICS_ROUTE_NONE);
  conn->close_after = 1;
  isere_response_simple(&conn->req, status, "text/plain", msg, 0);
  (void)msg;
  isere_request_complete(&conn->req);
}

static int __method_matches(const char *route_method, int is_head, const char *buf, http_span_t method)
{
  if (route_method[0] == '*' && route_method[1] == '\0') {
    return 1;
  }
  if (http_span_is(buf, method, route_method)) {
    return 1;
  }
  // HEAD is served by GET routes, without the body
  return is_head && route_method[0] == 'G' && route_method[1] == 'E' && route_method[2] == 'T' && route_method[3] == '\0';
}

static void __builtin_version(isere_request_t *req)
{
  char body[160];
  int n = snprintf(body, sizeof(body), "{\"build\":\"%s\",\"runtime\":\"%s\",\"js_mode\":\"%s\"}\n",
    ISERE_BUILD_ID, ISERE_RUNTIME_NAME, isere_engine_mode());
  isere_response_simple(req, 200, "application/json", body, n > 0 ? (size_t)n : 0);
}

static void __builtin_metrics(isere_request_t *req)
{
  size_t len = 0;
  char *json = isere_metrics_json(&len);
  if (json == NULL) {
    static const char msg[] = "out of memory\n";
    isere_response_simple(req, 503, "text/plain", msg, sizeof(msg) - 1);
    return;
  }
  isere_response_simple(req, 200, "application/json", json, len);
  isere_free(json);
}

static void __handle(httpd_conn_t *conn)
{
  const char *buf = conn->buf;
  http_span_t path = conn->head.path;
  http_span_t method = conn->head.method;
  conn->is_head = http_span_is(buf, method, "HEAD");
  int get = conn->is_head || http_span_is(buf, method, "GET");

  // built-in endpoints, answered without touching a JS engine
  int version = http_span_is(buf, path, "/__version");
  if (version || http_span_is(buf, path, "/__metrics")) {
    __begin_request(conn, ISERE_METRICS_ROUTE_NONE);
    if (!get) {
      isere_response_start(&conn->req, 405);
      isere_response_header(&conn->req, "Allow", 5, "GET, HEAD", 9);
      isere_response_finish(&conn->req, NULL, 0);
    } else if (version) {
      __builtin_version(&conn->req);
    } else {
      // ?gc=1 collects garbage first, so heap figures don't depend on when the GC last ran
      if (http_span_is(buf, conn->head.query, "gc=1")) {
        isere_engine_gc();
      }
      __builtin_metrics(&conn->req);
    }
    isere_request_complete(&conn->req);
    return;
  }

  int route = -1;
  int path_known = 0;
  for (uint32_t i = 0; i < isere_app.num_routes; i++) {
    const isere_app_route_t *r = &isere_app.routes[i];
    if (!http_span_is(buf, path, r->path)) {
      continue;
    }
    path_known = 1;
    if (__method_matches(r->method, conn->is_head, buf, method)) {
      route = (int)i;
      break;
    }
  }

  if (route < 0) {
    // unknown routes never reach a JS engine
    __begin_request(conn, ISERE_METRICS_ROUTE_NONE);
    static const char not_found[] = "not found\n";
    static const char not_allowed[] = "method not allowed\n";
    if (path_known) {
      isere_response_simple(&conn->req, 405, "text/plain", not_allowed, sizeof(not_allowed) - 1);
    } else {
      isere_response_simple(&conn->req, 404, "text/plain", not_found, sizeof(not_found) - 1);
    }
    isere_request_complete(&conn->req);
    return;
  }

  __begin_request(conn, route);
  conn->req.body.off = (uint32_t)conn->head_len;
  conn->req.body.len = (uint32_t)(conn->framing.kind == HTTP_BODY_CHUNKED ? conn->framing.content_length
    : (conn->framing.kind == HTTP_BODY_LENGTH ? conn->framing.content_length : 0));

  const isere_app_route_t *r = &isere_app.routes[route];
  if (r->kind == ISERE_ROUTE_STATIC) {
    isere_response_simple(&conn->req, 200, r->content_type, r->data, r->data_len);
    isere_request_complete(&conn->req);
    return;
  }

  if (isere_engine_dispatch(&conn->req) < 0) {
    static const char msg[] = "handler unavailable\n";
    isere_response_simple(&conn->req, 500, "text/plain", msg, sizeof(msg) - 1);
    isere_request_complete(&conn->req);
  }
}

// grows conn->buf to at least `want` bytes, up to `limit`
static int __grow(httpd_conn_t *conn, size_t want, size_t limit)
{
  if (want <= conn->cap) {
    return 0;
  }
  size_t cap = conn->cap * 2;
  if (cap < want) cap = want;
  if (cap > limit) cap = limit;
  if (cap < want) {
    return -1;
  }
  char *buf = isere_realloc(conn->buf, cap);
  if (buf == NULL) {
    return -1;
  }
  conn->buf = buf;
  conn->cap = cap;
  return 0;
}

// 1: a complete request is ready, 0: waiting for bytes, -1: rejected
static int __try_parse(httpd_conn_t *conn)
{
  if (conn->len == 0) {
    return 0;
  }

  if (conn->head_len == 0) {
    int r;
    for (;;) {
      r = http_parse_request(conn->buf, conn->len, &conn->head, conn->headers, conn->headers_cap);
      if (r != HTTP_PARSE_TOO_MANY) {
        break;
      }
      // at least four bytes per header line, so the head budget bounds this
      size_t cap = conn->headers_cap * 2;
      if (cap > __s.head_budget / 4) {
        __reject(conn, 431);
        return -1;
      }
      http_header_t *headers = isere_realloc(conn->headers, cap * sizeof(http_header_t));
      if (headers == NULL) {
        __reject(conn, 503);
        return -1;
      }
      conn->headers = headers;
      conn->headers_cap = cap;
    }

    if (r == HTTP_PARSE_INCOMPLETE) {
      if (conn->len >= __s.head_budget) {
        __reject(conn, 431);
        return -1;
      }
      return 0;
    }
    if (r == HTTP_PARSE_ERROR) {
      __reject(conn, 400);
      return -1;
    }
    if ((size_t)r > __s.head_budget) {
      __reject(conn, 431);
      return -1;
    }
    conn->head_len = (size_t)r;

    if (http_request_framing(conn->buf, conn->headers, conn->head.num_headers, conn->head.minor_version,
        &conn->framing) < 0) {
      __reject(conn, 400);
      return -1;
    }
    if (conn->framing.kind == HTTP_BODY_LENGTH && conn->framing.content_length > __s.body_budget) {
      __reject(conn, 413);
      return -1;
    }
  }

  size_t have = conn->len - conn->head_len;
  switch (conn->framing.kind) {
    case HTTP_BODY_NONE:
      conn->consumed = conn->head_len;
      return 1;

    case HTTP_BODY_LENGTH: {
      size_t need = conn->head_len + (size_t)conn->framing.content_length;
      if (conn->len >= need) {
        conn->consumed = need;
        return 1;
      }
      // size the buffer for exactly this request
      if (__grow(conn, need, need) < 0) {
        __reject(conn, 503);
        return -1;
      }
      break;
    }

    case HTTP_BODY_CHUNKED: {
      int64_t n = http_chunked_length(conn->buf + conn->head_len, have);
      if (n == HTTP_PARSE_ERROR) {
        __reject(conn, 400);
        return -1;
      }
      if (n == HTTP_PARSE_INCOMPLETE) {
        // the encoded body may carry some framing on top of the body budget
        if (have > __s.body_budget + __s.body_budget / 4 + 64) {
          __reject(conn, 413);
          return -1;
        }
        break;
      }
      size_t decoded = http_chunked_decode(conn->buf + conn->head_len, (size_t)n);
      if (decoded > __s.body_budget) {
        __reject(conn, 413);
        return -1;
      }
      // the decoded body now sits right after the head; remember its length in the framing
      conn->framing.content_length = decoded;
      conn->consumed = conn->head_len + (size_t)n;
      return 1;
    }

    default:
      __reject(conn, 400);
      return -1;
  }

  // body still to come
  const http_header_t *expect = http_find_header(conn->buf, conn->headers, conn->head.num_headers, "expect");
  if (expect != NULL && !conn->sent_continue && http_span_equals(conn->buf, expect->value, "100-continue")) {
    static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
    isere_tcp_write(conn->fd, cont, sizeof(cont) - 1);
    conn->sent_continue = 1;
  }
  return 0;
}

static void __process(httpd_conn_t *conn)
{
  conn->processing = 1;
  while (conn->state == CONN_READ && !conn->dead) {
    if (__try_parse(conn) <= 0) {
      break;
    }
    __handle(conn);
  }
  conn->processing = 0;
}

static void __on_timeout(isere_timer_t *timer)
{
  httpd_conn_t *conn = (httpd_conn_t *)timer->opaque;
  ENTER(conn);
  isere_metrics_conn(ISERE_CONN_TIMED_OUT);
  if (conn->state == CONN_READ && conn->len > 0) {
    __reject(conn, 408);
  } else {
    __conn_close(conn);
  }
  LEAVE(conn);
}

static void __request_done(httpd_conn_t *conn)
{
  conn->served++;
  isere_free(conn->out);
  conn->out = NULL;
  conn->out_cap = 0;
  conn->out_len = 0;
  conn->out_sent = 0;

  if (conn->close_after || __s.httpd->should_exit) {
    __conn_close(conn);
    return;
  }

  // drop this request's bytes; a pipelined request may follow them
  size_t rest = conn->len - conn->consumed;
  if (rest > 0) {
    memmove(conn->buf, conn->buf + conn->consumed, rest);
  }
  conn->len = rest;
  __reset_request(conn);

  // give back what a large request needed
  if (conn->cap > ISERE_HTTPD_INITIAL_BUFFER * 2 && conn->len <= ISERE_HTTPD_INITIAL_BUFFER) {
    char *buf = isere_realloc(conn->buf, ISERE_HTTPD_INITIAL_BUFFER);
    if (buf != NULL) {
      conn->buf = buf;
      conn->cap = ISERE_HTTPD_INITIAL_BUFFER;
    }
  }
  if (conn->headers_cap > 16) {
    http_header_t *headers = isere_realloc(conn->headers, 8 * sizeof(http_header_t));
    if (headers != NULL) {
      conn->headers = headers;
      conn->headers_cap = 8;
    }
  }

  conn->state = CONN_READ;
  isere_timer_start(&conn->timer, conn->len > 0 ? ISERE_HTTPD_HEAD_TIMEOUT_MS : ISERE_HTTPD_IDLE_TIMEOUT_MS);
  conn->head_timer = conn->len > 0;
  uv__io_start(isere_loop(), &conn->w, UV_POLLIN);

  if (!conn->processing) {
    __process(conn);
  }
}

static void __flush(httpd_conn_t *conn)
{
  while (conn->out_sent < conn->out_len) {
    ssize_t n = isere_tcp_write(conn->fd, conn->out + conn->out_sent, conn->out_len - conn->out_sent);
    if (n == ISERE_TCP_AGAIN) {
      uv__io_start(isere_loop(), &conn->w, UV_POLLOUT);
      // a client that stops reading doesn't hold the connection forever
      isere_timer_start(&conn->timer, ISERE_HTTPD_HEAD_TIMEOUT_MS);
      return;
    }
    if (n <= 0) {
      __conn_close(conn);
      return;
    }
    conn->out_sent += (size_t)n;
  }

  uv__io_stop(isere_loop(), &conn->w, UV_POLLOUT);
  isere_timer_stop(&conn->timer);
  __request_done(conn);
}

static void __on_readable(httpd_conn_t *conn)
{
  if (conn->len == conn->cap) {
    size_t limit = conn->head_len == 0 ? __s.head_budget
      : conn->head_len + __s.body_budget + __s.body_budget / 4 + 64;
    if (__grow(conn, conn->cap + 1, limit) < 0) {
      __reject(conn, conn->head_len == 0 ? 431 : 413);
      return;
    }
  }

  ssize_t n = isere_tcp_recv(conn->fd, conn->buf + conn->len, conn->cap - conn->len);
  if (n == ISERE_TCP_AGAIN) {
    return;
  }
  if (n <= 0) {
    __conn_close(conn);
    return;
  }

  if (conn->len == 0 && !conn->head_timer) {
    isere_timer_start(&conn->timer, ISERE_HTTPD_HEAD_TIMEOUT_MS);
    conn->head_timer = 1;
  }
  conn->len += (size_t)n;
  __process(conn);
}

static void __on_io(uv_loop_t *loop, uv__io_t *w, unsigned int events)
{
  httpd_conn_t *conn = (httpd_conn_t *)w->opaque;
  ENTER(conn);

  if ((events & UV_POLLOUT) && conn->state == CONN_WRITE) {
    __flush(conn);
  } else if ((events & UV_POLLIN) && conn->state == CONN_READ) {
    __on_readable(conn);
  } else if (events & (POLLERR | POLLHUP)) {
    if (conn->state != CONN_HANDLE) {
      __conn_close(conn);
    }
  }

  LEAVE(conn);
}

// closes the oldest idle keep-alive connection to make room for a new one
static int __evict_idle(void)
{
  struct uv__queue *q;
  uv__queue_foreach(q, &__s.conns) {
    httpd_conn_t *conn = uv__queue_data(q, httpd_conn_t, node);
    if (!conn->self && conn->state == CONN_READ && conn->len == 0 && conn->served > 0 && !conn->dead) {
      isere_metrics_conn(ISERE_CONN_EVICTED);
      __conn_close(conn);
      return 1;
    }
  }
  return 0;
}

static void __on_connection(uv_loop_t *loop, uv__io_t *w, unsigned int events)
{
  if ((events & UV_POLLIN) == 0) {
    return;
  }

  for (;;) {
    char ip[16];
    uint16_t port = 0;
    int fd = isere_tcp_accept(__s.serverfd, ip, &port);
    if (fd < 0) {
      return;
    }

    int self = ip[0] == '1' && strcmp(ip, "127.0.0.1") == 0 && isere_fetch_is_own_port(port);
    int room = self ? __s.active_self < ISERE_HTTPD_SELF_RESERVE
      : (__s.active < __s.max_conns || __evict_idle());
    if (!room) {
      static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\nRetry-After: 1\r\n\r\n";
      isere_tcp_write(fd, busy, sizeof(busy) - 1);
      isere_tcp_close(fd);
      isere_metrics_conn(ISERE_CONN_REJECTED);
      continue;
    }

    httpd_conn_t *conn = isere_calloc(1, sizeof(httpd_conn_t));
    char *buf = isere_malloc(ISERE_HTTPD_INITIAL_BUFFER);
    http_header_t *headers = isere_malloc(8 * sizeof(http_header_t));
    if (conn == NULL || buf == NULL || headers == NULL) {
      isere_free(conn);
      isere_free(buf);
      isere_free(headers);
      isere_tcp_close(fd);
      isere_metrics_conn(ISERE_CONN_REJECTED);
      continue;
    }

    conn->fd = fd;
    conn->self = self;
    conn->buf = buf;
    conn->cap = ISERE_HTTPD_INITIAL_BUFFER;
    conn->headers = headers;
    conn->headers_cap = 8;
    conn->state = CONN_READ;
    uv__queue_insert_tail(&__s.conns, &conn->node);
    if (self) {
      __s.active_self++;
    } else {
      __s.active++;
    }
    isere_metrics_conn(ISERE_CONN_ACCEPTED);

    conn->w.fd = fd;
    conn->w.cb = __on_io;
    conn->w.opaque = conn;
    uv__queue_init(&conn->w.watcher_queue);
    uv__io_start(isere_loop(), &conn->w, UV_POLLIN);

    isere_timer_init(&conn->timer, __on_timeout, conn);
    isere_timer_start(&conn->timer, ISERE_HTTPD_HEAD_TIMEOUT_MS);
  }
}

// ---------------------------------------------------------------------------
// the task

static void __on_tick(isere_timer_t *timer)
{
  isere_metrics_tick();
  isere_timer_start(timer, 1000);
}

static size_t __env_size(const char *name, size_t fallback)
{
  const char *v = getenv(name);
  return (v != NULL && atol(v) > 0) ? (size_t)atol(v) : fallback;
}

static void __httpd_task(void *param)
{
  isere_httpd_t *httpd = __s.httpd;
  int failed = 1;

  // measures how much of its stack this task uses (reported at /__metrics)
  platform_stack_watch(ISERE_HTTPD_TASK_STACK_SIZE * sizeof(StackType_t));

  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  isere_loop_init();
  isere_fetch_init();

  if (isere_engine_init() < 0) {
    httpd->logger->error(ISERE_HTTPD_LOG_TAG, "unable to start the JS engine");
    goto exit;
  }

  if ((__s.serverfd = isere_tcp_socket_new()) < 0 ||
      isere_tcp_socket_set_reuse(__s.serverfd) < 0 ||
      isere_tcp_listen(__s.serverfd, ISERE_HTTPD_PORT) < 0) {
    httpd->logger->error(ISERE_HTTPD_LOG_TAG, "unable to listen on port %d", ISERE_HTTPD_PORT);
    goto exit;
  }

  __s.w.fd = __s.serverfd;
  __s.w.cb = __on_connection;
  __s.w.opaque = NULL;
  uv__queue_init(&__s.w.watcher_queue);
  uv__io_start(isere_loop(), &__s.w, UV_POLLIN);

  isere_timer_init(&__s.tick, __on_tick, NULL);
  isere_timer_start(&__s.tick, 1000);

  httpd->logger->info(ISERE_HTTPD_LOG_TAG, "Listening on port %d (%s, %s)", ISERE_HTTPD_PORT,
    ISERE_RUNTIME_NAME, isere_engine_mode());

  while (!httpd->should_exit) {
    // bounded so a shutdown request is noticed promptly
    isere_loop_run_once(100);
  }

  httpd->logger->info(ISERE_HTTPD_LOG_TAG, "shutting down");
  failed = 0;

exit:
  isere_engine_deinit();

  while (!uv__queue_empty(&__s.conns)) {
    httpd_conn_t *conn = uv__queue_data(uv__queue_head(&__s.conns), httpd_conn_t, node);
    conn->depth = 0;
    __conn_free(conn);
  }
  if (__s.serverfd >= 0) {
    uv__io_stop(isere_loop(), &__s.w, UV_POLLIN);
    isere_tcp_close(__s.serverfd);
  }
  isere_timer_stop(&__s.tick);
  isere_fetch_deinit();
  isere_loop_deinit();
  isere_metrics_deinit();
  isere_free(__s.route_methods);
  isere_free(__s.route_paths);

  // main() turns this into the exit code
  httpd->should_exit = failed ? 3 : 2;
  vTaskEndScheduler();
  vTaskDelete(NULL);
}

int isere_httpd_init(isere_httpd_t *httpd, isere_logger_t *logger)
{
  memset(&__s, 0, sizeof(__s));
  __s.httpd = httpd;
  __s.serverfd = -1;
  httpd->logger = logger;
  httpd->should_exit = 0;
  uv__queue_init(&__s.conns);

  __s.max_conns = (uint32_t)__env_size("ISERE_MAX_CONNECTIONS", ISERE_TCP_MAX_CONNECTIONS);
  __s.head_budget = __env_size("ISERE_HEAD_BUDGET", ISERE_HTTPD_HEAD_BUDGET);
  __s.body_budget = __env_size("ISERE_BODY_BUDGET", ISERE_HTTPD_BODY_BUDGET);

  __s.route_methods = isere_calloc(isere_app.num_routes + 1, sizeof(char *));
  __s.route_paths = isere_calloc(isere_app.num_routes + 1, sizeof(char *));
  if (__s.route_methods == NULL || __s.route_paths == NULL) {
    return -1;
  }
  for (uint32_t i = 0; i < isere_app.num_routes; i++) {
    __s.route_methods[i] = isere_app.routes[i].method;
    __s.route_paths[i] = isere_app.routes[i].path;
  }
  isere_metrics_init(isere_app.num_routes, __s.route_methods, __s.route_paths);

  if (xTaskCreate(__httpd_task, "httpd", ISERE_HTTPD_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &httpd->tsk) != pdPASS) {
    logger->error(ISERE_HTTPD_LOG_TAG, "Unable to create httpd task");
    return -1;
  }

  return 0;
}

int isere_httpd_deinit(isere_httpd_t *httpd)
{
  return 0;
}
