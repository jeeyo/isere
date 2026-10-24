#ifndef ISERE_REQUEST_H_
#define ISERE_REQUEST_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "http_parser.h"
#include "loop.h"
#include "metrics.h"

// One HTTP request on its way through isère. The method, path, headers and
// body are spans into the connection's receive buffer; they stay valid until
// the response has been sent.
typedef struct isere_request_s {
  uint32_t id;
  int route;                      // index into isere_app.routes
  const char *buf;
  const http_request_head_t *head;
  const http_header_t *headers;
  http_span_t body;

  uint64_t deadline_us;
  isere_timer_t deadline;
  int responded;                  // a response is queued; the engine may still be cleaning up
  int status;

  isere_request_metrics_t metrics;
  void *engine;                   // the engine's per-request state
  void *conn;                     // the httpd connection
} isere_request_t;

// Building the response. Header values are written exactly as given, so they are
// checked for CR, LF and NUL; a bad header is dropped. Headers that describe the
// framing (Content-Length, Transfer-Encoding, Connection) are set by isère.
void isere_response_start(isere_request_t *req, int status);
void isere_response_header(isere_request_t *req, const char *name, size_t name_len,
  const char *value, size_t value_len);
// copies the body into the connection's output buffer and queues the response
void isere_response_finish(isere_request_t *req, const char *body, size_t body_len);

// the same in two steps, for callers that write the body straight into the output
// buffer: isere_response_body() returns room for body_len bytes (NULL when out of
// memory), isere_response_end() queues the response
char *isere_response_body(isere_request_t *req, size_t body_len);
void isere_response_end(isere_request_t *req);

// convenience for errors and built-ins
void isere_response_simple(isere_request_t *req, int status, const char *content_type,
  const char *body, size_t body_len);

// the engine is done with a request: the response (queued earlier) can go out
// and the connection can move on to the next request
void isere_request_complete(isere_request_t *req);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_REQUEST_H_ */
