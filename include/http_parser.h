#ifndef ISERE_HTTP_PARSER_H_
#define ISERE_HTTP_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// A stateless HTTP/1.x head parser in the style of picohttpparser.
//
// It parses from the start of a buffer every time it's called and records every
// field as an {offset, len} span into that buffer, so nothing is copied and the
// buffer may be moved or grown between calls. Call it again whenever more
// bytes arrive; the head is small, so re-parsing costs little.

typedef struct {
  uint32_t off;
  uint32_t len;
} http_span_t;

typedef struct {
  http_span_t name;
  http_span_t value;
} http_header_t;

typedef struct {
  http_span_t method;
  http_span_t target;   // path and query as sent
  http_span_t path;
  http_span_t query;    // after '?', empty if none
  int minor_version;    // HTTP/1.<minor>
  size_t num_headers;
} http_request_head_t;

typedef struct {
  int minor_version;
  int status;
  http_span_t reason;
  size_t num_headers;
} http_response_head_t;

// return values of the parse functions; a positive value is the head's length in bytes
#define HTTP_PARSE_INCOMPLETE   (-2)  // need more bytes
#define HTTP_PARSE_ERROR        (-1)  // malformed: answer 400
#define HTTP_PARSE_TOO_MANY     (-3)  // `headers` is too small: grow it and call again

int http_parse_request(const char *buf, size_t len, http_request_head_t *req,
  http_header_t *headers, size_t max_headers);

int http_parse_response(const char *buf, size_t len, http_response_head_t *resp,
  http_header_t *headers, size_t max_headers);

// How the body of a message is delimited, decided from its headers.
typedef enum {
  HTTP_BODY_NONE = 0,
  HTTP_BODY_LENGTH,     // Content-Length
  HTTP_BODY_CHUNKED,    // Transfer-Encoding: chunked
  HTTP_BODY_UNTIL_CLOSE // responses only: read until the peer closes
} http_body_kind_t;

typedef struct {
  http_body_kind_t kind;
  uint64_t content_length;
  int keep_alive;
} http_framing_t;

// Returns 0, or HTTP_PARSE_ERROR for ambiguous or invalid framing: both
// Content-Length and Transfer-Encoding, more than one Content-Length, a
// Transfer-Encoding other than "chunked", or a malformed length.
int http_request_framing(const char *buf, const http_header_t *headers, size_t num_headers,
  int minor_version, http_framing_t *framing);
int http_response_framing(const char *buf, const http_header_t *headers, size_t num_headers,
  int minor_version, int status, int is_head_request, http_framing_t *framing);

// Checks a chunked body in buf[0, len). Returns HTTP_PARSE_INCOMPLETE,
// HTTP_PARSE_ERROR, or the number of bytes the encoded body occupies
// (including the last chunk and trailers). Leaves the buffer untouched.
int64_t http_chunked_length(const char *buf, size_t len);

// Decodes a complete chunked body in place, returning the decoded length.
// Only call it once http_chunked_length() has returned a length.
size_t http_chunked_decode(char *buf, size_t len);

// case-insensitive comparison of a span with a NUL-terminated literal
int http_span_equals(const char *buf, http_span_t span, const char *literal);

// first header named `name` (case-insensitive), or NULL
const http_header_t *http_find_header(const char *buf, const http_header_t *headers,
  size_t num_headers, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTP_PARSER_H_ */
