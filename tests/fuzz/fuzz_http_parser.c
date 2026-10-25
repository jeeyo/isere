// libFuzzer target for src/http_parser.c, built with AddressSanitizer:
//   tools/fuzz.sh [seconds]
//
// Besides crashes and sanitizer reports, it checks that every span the parser
// returns lies inside the input and that chunked decoding never grows the body.

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "http_parser.h"

static void check_span(http_span_t s, size_t len)
{
  assert((size_t)s.off <= len);
  assert((size_t)s.len <= len - s.off);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  // a private copy, so writing outside the input is caught by ASan
  char *buf = malloc(size ? size : 1);
  memcpy(buf, data, size);

  size_t cap = 2;
  http_header_t *headers = malloc(cap * sizeof(*headers));
  http_request_head_t req;
  int r;
  while ((r = http_parse_request(buf, size, &req, headers, cap)) == HTTP_PARSE_TOO_MANY) {
    cap *= 2;
    headers = realloc(headers, cap * sizeof(*headers));
  }

  if (r > 0) {
    assert((size_t)r <= size);
    check_span(req.method, size);
    check_span(req.target, size);
    check_span(req.path, size);
    check_span(req.query, size);
    for (size_t i = 0; i < req.num_headers; i++) {
      check_span(headers[i].name, size);
      check_span(headers[i].value, size);
      assert(headers[i].name.len > 0);
    }

    http_framing_t framing;
    if (http_request_framing(buf, headers, req.num_headers, req.minor_version, &framing) == 0 &&
        framing.kind == HTTP_BODY_CHUNKED) {
      size_t body_len = size - (size_t)r;
      int64_t n = http_chunked_length(buf + r, body_len);
      if (n >= 0) {
        assert((size_t)n <= body_len);
        size_t decoded = http_chunked_decode(buf + r, (size_t)n);
        assert(decoded <= (size_t)n);
      }
    }
  }

  // the same bytes as a response head
  http_response_head_t resp;
  while ((r = http_parse_response(buf, size, &resp, headers, cap)) == HTTP_PARSE_TOO_MANY) {
    cap *= 2;
    headers = realloc(headers, cap * sizeof(*headers));
  }
  if (r > 0) {
    assert(resp.status >= 0 && resp.status <= 999);
    check_span(resp.reason, size);
    http_framing_t framing;
    http_response_framing(buf, headers, resp.num_headers, resp.minor_version, resp.status, 0, &framing);
  }

  // and as a bare chunked body
  int64_t n = http_chunked_length(buf, size);
  if (n >= 0) {
    assert((size_t)n <= size);
    assert(http_chunked_decode(buf, (size_t)n) <= (size_t)n);
  }

  free(headers);
  free(buf);
  return 0;
}
