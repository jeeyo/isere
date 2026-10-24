#include "fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "loop.h"
#include "mem.h"
#include "tcp.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"

#ifndef ISERE_FETCH_TIMEOUT_MS
#define ISERE_FETCH_TIMEOUT_MS 10000
#endif
// a response larger than this fails the fetch
#ifndef ISERE_FETCH_MAX_RESPONSE
#define ISERE_FETCH_MAX_RESPONSE (128 * 1024)
#endif
#ifndef ISERE_FETCH_CA_FILE
#define ISERE_FETCH_CA_FILE "/etc/ssl/certs/ca-certificates.crt"
#endif

typedef enum {
  FETCH_CONNECTING = 0,
  FETCH_HANDSHAKE,
  FETCH_SENDING,
  FETCH_RECEIVING,
} fetch_state_t;

typedef struct {
  struct uv__queue node;
  js_vm_t *vm;
  uint32_t request_id;
  js_ref_t fn;

  int fd;
  uint16_t local_port;
  uv__io_t w;
  isere_timer_t timer;
  fetch_state_t state;
  int is_head;
  const char *early_error;    // failed before connecting; reported from a timer, never re-entrantly

  mbedtls_ssl_context *ssl;   // NULL for plain HTTP
  char host[256];

  char *out;
  size_t out_len;
  size_t out_sent;

  char *in;
  size_t in_len;
  size_t in_cap;
  http_header_t *headers;
  size_t headers_cap;
} fetch_t;

static struct {
  struct uv__queue fetches;
  int tls_ready;
  mbedtls_ssl_config conf;
  mbedtls_x509_crt ca;
} __f;

void isere_fetch_init(void)
{
  memset(&__f, 0, sizeof(__f));
  uv__queue_init(&__f.fetches);
}

static void __free(fetch_t *f)
{
  uv__io_stop(isere_loop(), &f->w, UV_POLLIN | UV_POLLOUT);
  if (f->fd >= 0) {
    uv__platform_invalidate_fd(isere_loop(), f->fd);
    isere_tcp_close(f->fd);
  }
  isere_timer_stop(&f->timer);
  uv__queue_remove(&f->node);
  if (f->ssl != NULL) {
    mbedtls_ssl_free(f->ssl);
    isere_free(f->ssl);
  }
  isere_free(f->out);
  isere_free(f->in);
  isere_free(f->headers);
  isere_free(f);
}

void isere_fetch_deinit(void)
{
  while (!uv__queue_empty(&__f.fetches)) {
    fetch_t *f = uv__queue_data(uv__queue_head(&__f.fetches), fetch_t, node);
    js_vm_release(f->vm, f->fn);
    __free(f);
  }
  if (__f.tls_ready) {
    mbedtls_ssl_config_free(&__f.conf);
    mbedtls_x509_crt_free(&__f.ca);
    __f.tls_ready = 0;
  }
  isere_crypto_deinit();
}

int isere_fetch_is_own_port(uint16_t port)
{
  struct uv__queue *q;
  uv__queue_foreach(q, &__f.fetches) {
    fetch_t *f = uv__queue_data(q, fetch_t, node);
    if (f->local_port == port) {
      return 1;
    }
  }
  return 0;
}

static void __cancel_where(js_vm_t *vm, uint32_t request_id)
{
  struct uv__queue *q = uv__queue_head(&__f.fetches);
  while (q != &__f.fetches) {
    fetch_t *f = uv__queue_data(q, fetch_t, node);
    q = uv__queue_next(q);
    if ((vm != NULL && f->vm == vm) || (request_id != 0 && f->request_id == request_id)) {
      js_vm_release(f->vm, f->fn);
      __free(f);
    }
  }
}

void isere_fetch_cancel_request(uint32_t request_id)
{
  // a fetch outlives the request that started it in warm mode: another request
  // may be waiting on the same promise (the shared JWKS refresh). It keeps
  // running, but no longer counts against the finished request.
  struct uv__queue *q;
  uv__queue_foreach(q, &__f.fetches) {
    fetch_t *f = uv__queue_data(q, fetch_t, node);
    if (f->request_id == request_id) {
      f->request_id = 0;
    }
  }
}

void isere_fetch_cancel_vm(js_vm_t *vm)
{
  __cancel_where(vm, 0);
}

// finishes a fetch: its callback runs with the result, then it is freed
static void __finish(fetch_t *f, const char *error)
{
  isere_fetch_result_t result;
  memset(&result, 0, sizeof(result));
  result.error = error;

  http_response_head_t head;
  http_framing_t framing;
  if (error == NULL) {
    int r = http_parse_response(f->in, f->in_len, &head, f->headers, f->headers_cap);
    if (r < 0 || http_response_framing(f->in, f->headers, head.num_headers, head.minor_version,
        head.status, f->is_head, &framing) < 0) {
      result.error = "malformed response";
    } else {
      result.status = head.status;
      result.buf = f->in;
      result.headers = f->headers;
      result.num_headers = head.num_headers;
      result.body = f->in + r;
      result.body_len = f->in_len - (size_t)r;
      if (framing.kind == HTTP_BODY_LENGTH && result.body_len > framing.content_length) {
        result.body_len = (size_t)framing.content_length;
      } else if (framing.kind == HTTP_BODY_CHUNKED) {
        result.body_len = http_chunked_decode(f->in + r, result.body_len);
      }
    }
  }

  // the callback may start or cancel fetches, so take this one off the list first
  uv__queue_remove(&f->node);
  uv__queue_init(&f->node);
  uv__io_stop(isere_loop(), &f->w, UV_POLLIN | UV_POLLOUT);
  isere_timer_stop(&f->timer);

  isere_engine_fetch_done(f->vm, f->request_id, f->fn, &result);

  uv__queue_insert_tail(&__f.fetches, &f->node);
  __free(f);
}

// 1 when the whole response is in f->in, 0 to keep reading, -1 if it is malformed
static int __response_complete(fetch_t *f, int eof)
{
  http_response_head_t head;
  int r;
  for (;;) {
    r = http_parse_response(f->in, f->in_len, &head, f->headers, f->headers_cap);
    if (r != HTTP_PARSE_TOO_MANY) {
      break;
    }
    size_t cap = f->headers_cap * 2;
    http_header_t *headers = isere_realloc(f->headers, cap * sizeof(http_header_t));
    if (headers == NULL) {
      return -1;
    }
    f->headers = headers;
    f->headers_cap = cap;
  }
  if (r == HTTP_PARSE_INCOMPLETE) {
    return eof ? -1 : 0;
  }
  if (r < 0) {
    return -1;
  }
  if (head.status >= 100 && head.status < 200) {
    // an interim response (100 Continue): drop it and wait for the real one
    memmove(f->in, f->in + r, f->in_len - (size_t)r);
    f->in_len -= (size_t)r;
    return __response_complete(f, eof);
  }

  http_framing_t framing;
  if (http_response_framing(f->in, f->headers, head.num_headers, head.minor_version, head.status,
      f->is_head, &framing) < 0) {
    return -1;
  }
  size_t have = f->in_len - (size_t)r;
  switch (framing.kind) {
    case HTTP_BODY_NONE:
      return 1;
    case HTTP_BODY_LENGTH:
      if (have >= framing.content_length) return 1;
      return eof ? -1 : 0;
    case HTTP_BODY_CHUNKED: {
      int64_t n = http_chunked_length(f->in + r, have);
      if (n >= 0) return 1;
      if (n == HTTP_PARSE_ERROR) return -1;
      return eof ? -1 : 0;
    }
    case HTTP_BODY_UNTIL_CLOSE:
      return eof ? 1 : 0;
  }
  return -1;
}

static int __bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  fetch_t *f = (fetch_t *)ctx;
  ssize_t n = isere_tcp_write(f->fd, (const char *)buf, len);
  if (n == ISERE_TCP_AGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
  if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
  return (int)n;
}

static int __bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  fetch_t *f = (fetch_t *)ctx;
  ssize_t n = isere_tcp_recv(f->fd, (char *)buf, len);
  if (n == ISERE_TCP_AGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
  if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
  return (int)n;
}

// waits for whichever direction mbedTLS (or the socket) needs next
static void __want(fetch_t *f, int events)
{
  uv__io_stop(isere_loop(), &f->w, (UV_POLLIN | UV_POLLOUT) & ~events);
  uv__io_start(isere_loop(), &f->w, events);
}

static void __step(fetch_t *f);

static void __on_io(uv_loop_t *loop, uv__io_t *w, unsigned int events)
{
  __step((fetch_t *)w->opaque);
}

static void __on_timeout(isere_timer_t *timer)
{
  fetch_t *f = (fetch_t *)timer->opaque;
  __finish(f, f->early_error != NULL ? f->early_error : "timed out");
}

// the JS caller is still on the stack: report the failure on the next loop turn
static int __fail_later(fetch_t *f, const char *error)
{
  f->early_error = error;
  isere_timer_start(&f->timer, 0);
  return 0;
}

static void __step(fetch_t *f)
{
  if (f->state == FETCH_CONNECTING) {
    int err = isere_tcp_socket_last_error(f->fd);
    if (err != 0) {
      __finish(f, "connection refused");
      return;
    }
    f->state = f->ssl != NULL ? FETCH_HANDSHAKE : FETCH_SENDING;
  }

  if (f->state == FETCH_HANDSHAKE) {
    int r = mbedtls_ssl_handshake(f->ssl);
    if (r == MBEDTLS_ERR_SSL_WANT_READ) { __want(f, UV_POLLIN); return; }
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) { __want(f, UV_POLLOUT); return; }
    if (r != 0) {
      __finish(f, r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? "certificate verification failed" : "TLS handshake failed");
      return;
    }
    f->state = FETCH_SENDING;
  }

  if (f->state == FETCH_SENDING) {
    while (f->out_sent < f->out_len) {
      int n;
      if (f->ssl != NULL) {
        n = mbedtls_ssl_write(f->ssl, (const unsigned char *)f->out + f->out_sent, f->out_len - f->out_sent);
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE) { __want(f, UV_POLLOUT); return; }
        if (n == MBEDTLS_ERR_SSL_WANT_READ) { __want(f, UV_POLLIN); return; }
      } else {
        n = (int)isere_tcp_write(f->fd, f->out + f->out_sent, f->out_len - f->out_sent);
        if (n == ISERE_TCP_AGAIN) { __want(f, UV_POLLOUT); return; }
      }
      if (n <= 0) {
        __finish(f, "send failed");
        return;
      }
      f->out_sent += (size_t)n;
    }
    isere_free(f->out);
    f->out = NULL;
    f->state = FETCH_RECEIVING;
  }

  // FETCH_RECEIVING
  for (;;) {
    if (f->in_len == f->in_cap) {
      if (f->in_cap >= ISERE_FETCH_MAX_RESPONSE) {
        __finish(f, "response too large");
        return;
      }
      size_t cap = f->in_cap * 2;
      if (cap > ISERE_FETCH_MAX_RESPONSE) cap = ISERE_FETCH_MAX_RESPONSE;
      char *in = isere_realloc(f->in, cap);
      if (in == NULL) {
        __finish(f, "out of memory");
        return;
      }
      f->in = in;
      f->in_cap = cap;
    }

    int n;
    int eof = 0;
    if (f->ssl != NULL) {
      n = mbedtls_ssl_read(f->ssl, (unsigned char *)f->in + f->in_len, f->in_cap - f->in_len);
      if (n == MBEDTLS_ERR_SSL_WANT_READ) { __want(f, UV_POLLIN); return; }
      if (n == MBEDTLS_ERR_SSL_WANT_WRITE) { __want(f, UV_POLLOUT); return; }
      if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) eof = 1;
      else if (n < 0) {
        __finish(f, "receive failed");
        return;
      }
    } else {
      n = (int)isere_tcp_recv(f->fd, f->in + f->in_len, f->in_cap - f->in_len);
      if (n == ISERE_TCP_AGAIN) { __want(f, UV_POLLIN); return; }
      if (n == 0) eof = 1;
      else if (n < 0) {
        __finish(f, "receive failed");
        return;
      }
    }
    if (n > 0) {
      f->in_len += (size_t)n;
    }

    int done = __response_complete(f, eof);
    if (done < 0) {
      __finish(f, "malformed response");
      return;
    }
    if (done > 0) {
      __finish(f, NULL);
      return;
    }
    if (eof) {
      __finish(f, "connection closed early");
      return;
    }
  }
}

static int __tls_init(void)
{
  if (__f.tls_ready) {
    return 0;
  }
  if (isere_crypto_init() < 0) {
    return -1;
  }
  mbedtls_ssl_config_init(&__f.conf);
  mbedtls_x509_crt_init(&__f.ca);

  const char *ca_file = getenv("ISERE_CA_FILE");
  if (ca_file == NULL) {
    ca_file = ISERE_FETCH_CA_FILE;
  }
#if defined(MBEDTLS_FS_IO)
  if (mbedtls_x509_crt_parse_file(&__f.ca, ca_file) < 0) {
    fprintf(stderr, "[fetch] could not load CA certificates from %s\n", ca_file);
  }
#endif

  if (mbedtls_ssl_config_defaults(&__f.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
      MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    return -1;
  }
  mbedtls_ssl_conf_authmode(&__f.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&__f.conf, &__f.ca, NULL);
  mbedtls_ssl_conf_rng(&__f.conf, mbedtls_ctr_drbg_random, isere_crypto_drbg());
  __f.tls_ready = 1;
  return 0;
}

static int __is_token(const char *s, size_t len)
{
  if (len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c <= 0x20 || c >= 0x7f || c == ':' || c == '"' || c == '(' || c == ')' || c == ',' ||
        c == '/' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '@' ||
        c == '[' || c == '\\' || c == ']' || c == '{' || c == '}') {
      return 0;
    }
  }
  return 1;
}

static int __is_value(const char *s, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\r' || c == '\n' || c == '\0') return 0;
  }
  return 1;
}

static int __append(fetch_t *f, size_t *cap, const char *data, size_t len)
{
  if (f->out_len + len > *cap) {
    size_t n = *cap * 2 + len;
    char *out = isere_realloc(f->out, n);
    if (out == NULL) return -1;
    f->out = out;
    *cap = n;
  }
  memcpy(f->out + f->out_len, data, len);
  f->out_len += len;
  return 0;
}

#define APPEND_LITERAL(f, cap, s) __append(f, cap, s, sizeof(s) - 1)

int isere_fetch_start(js_vm_t *vm, const isere_fetch_request_t *request, js_ref_t fn)
{
  // scheme://host[:port][/path]
  const char *url = request->url;
  size_t url_len = request->url_len;
  int tls;
  size_t pos;
  if (url_len > 7 && memcmp(url, "http://", 7) == 0) {
    tls = 0;
    pos = 7;
  } else if (url_len > 8 && memcmp(url, "https://", 8) == 0) {
    tls = 1;
    pos = 8;
  } else {
    js_vm_release(vm, fn);
    return -1;
  }

  size_t host_start = pos;
  while (pos < url_len && url[pos] != '/' && url[pos] != '?' && url[pos] != ':' && url[pos] != '#') pos++;
  size_t host_len = pos - host_start;
  uint32_t port = tls ? 443 : 80;
  if (pos < url_len && url[pos] == ':') {
    port = 0;
    pos++;
    while (pos < url_len && url[pos] >= '0' && url[pos] <= '9') {
      port = port * 10 + (uint32_t)(url[pos] - '0');
      pos++;
    }
  }
  size_t path_start = pos;
  size_t path_end = pos;
  while (path_end < url_len && url[path_end] != '#') path_end++;
  if (host_len == 0 || host_len >= 256 || port == 0 || port > 65535) {
    js_vm_release(vm, fn);
    return -1;
  }

  fetch_t *f = isere_calloc(1, sizeof(fetch_t));
  if (f == NULL) {
    js_vm_release(vm, fn);
    return -1;
  }
  f->fd = -1;
  f->vm = vm;
  f->fn = fn;
  isere_request_t *req = isere_js_current_request();
  f->request_id = req != NULL ? req->id : 0;
  memcpy(f->host, url + host_start, host_len);
  f->host[host_len] = '\0';
  f->is_head = request->method_len == 4 && memcmp(request->method, "HEAD", 4) == 0;
  uv__queue_insert_tail(&__f.fetches, &f->node);
  isere_timer_init(&f->timer, __on_timeout, f);

  // the request, written once into f->out
  size_t cap = 256 + request->body_len;
  f->out = isere_malloc(cap);
  f->in_cap = 1024;
  f->in = isere_malloc(f->in_cap);
  f->headers_cap = 16;
  f->headers = isere_malloc(f->headers_cap * sizeof(http_header_t));
  if (f->out == NULL || f->in == NULL || f->headers == NULL || !__is_token(request->method, request->method_len)) {
    goto fail;
  }

  char num[16];
  int ok = __append(f, &cap, request->method, request->method_len) == 0 &&
    APPEND_LITERAL(f, &cap, " ") == 0 &&
    (path_end > path_start && url[path_start] == '/' ? 0 : APPEND_LITERAL(f, &cap, "/")) == 0 &&
    __append(f, &cap, url + path_start, path_end - path_start) == 0 &&
    APPEND_LITERAL(f, &cap, " HTTP/1.1\r\nHost: ") == 0 &&
    __append(f, &cap, f->host, host_len) == 0;
  if (ok && port != (tls ? 443u : 80u)) {
    int n = snprintf(num, sizeof(num), ":%u", (unsigned)port);
    ok = __append(f, &cap, num, (size_t)n) == 0;
  }
  ok = ok && APPEND_LITERAL(f, &cap, "\r\nConnection: close\r\nUser-Agent: isere\r\n") == 0;

  for (size_t i = 0; ok && i < request->num_headers; i++) {
    const char *name = request->header_names[i];
    size_t name_len = request->header_name_lens[i];
    const char *value = request->header_values[i];
    size_t value_len = request->header_value_lens[i];
    if (name == NULL || value == NULL || !__is_token(name, name_len) || !__is_value(value, value_len)) {
      continue;
    }
    http_span_t span = { 0, (uint32_t)name_len };
    if (http_span_equals(name, span, "host") || http_span_equals(name, span, "connection") ||
        http_span_equals(name, span, "content-length") || http_span_equals(name, span, "transfer-encoding")) {
      continue;
    }
    ok = __append(f, &cap, name, name_len) == 0 && APPEND_LITERAL(f, &cap, ": ") == 0 &&
      __append(f, &cap, value, value_len) == 0 && APPEND_LITERAL(f, &cap, "\r\n") == 0;
  }
  if (ok && request->body != NULL) {
    int n = snprintf(num, sizeof(num), "%zu", request->body_len);
    ok = APPEND_LITERAL(f, &cap, "Content-Length: ") == 0 && __append(f, &cap, num, (size_t)n) == 0 &&
      APPEND_LITERAL(f, &cap, "\r\n") == 0;
  }
  ok = ok && APPEND_LITERAL(f, &cap, "\r\n") == 0;
  if (ok && request->body != NULL && request->body_len > 0) {
    ok = __append(f, &cap, request->body, request->body_len) == 0;
  }
  if (!ok) {
    goto fail;
  }

  char ip[46];
  if (strcmp(f->host, "localhost") == 0) {
    memcpy(ip, "127.0.0.1", 10);
  } else if (isere_tcp_resolve(f->host, ip, sizeof(ip)) < 0) {
    // reported through the callback, like any network failure
    return __fail_later(f, "could not resolve host");
  }

  if (tls) {
    if (__tls_init() < 0) {
      goto fail;
    }
    f->ssl = isere_malloc(sizeof(mbedtls_ssl_context));
    if (f->ssl == NULL) {
      goto fail;
    }
    mbedtls_ssl_init(f->ssl);
    if (mbedtls_ssl_setup(f->ssl, &__f.conf) != 0 || mbedtls_ssl_set_hostname(f->ssl, f->host) != 0) {
      goto fail;
    }
    mbedtls_ssl_set_bio(f->ssl, f, __bio_send, __bio_recv, NULL);
  }

  f->fd = isere_tcp_socket_new();
  if (f->fd < 0) {
    goto fail;
  }
  int r = isere_tcp_connect(f->fd, ip, (uint16_t)port);
  // record the local port right away: httpd may accept our connection to it before we hear back
  int lp = isere_tcp_local_port(f->fd);
  f->local_port = lp > 0 ? (uint16_t)lp : 0;
  if (r < 0 && r != ISERE_TCP_AGAIN) {
    return __fail_later(f, "connection refused");
  }

  f->w.fd = f->fd;
  f->w.cb = __on_io;
  f->w.opaque = f;
  uv__queue_init(&f->w.watcher_queue);
  f->state = FETCH_CONNECTING;
  uv__io_start(isere_loop(), &f->w, UV_POLLOUT);
  isere_timer_start(&f->timer, ISERE_FETCH_TIMEOUT_MS);
  return 0;

fail:
  js_vm_release(vm, f->fn);
  __free(f);
  return -1;
}
