#include "http_parser.h"

#include <string.h>

// RFC 9110 tchar
static const uint8_t __tchar[256] = {
  ['!'] = 1, ['#'] = 1, ['$'] = 1, ['%'] = 1, ['&'] = 1, ['\''] = 1, ['*'] = 1,
  ['+'] = 1, ['-'] = 1, ['.'] = 1, ['^'] = 1, ['_'] = 1, ['`'] = 1, ['|'] = 1, ['~'] = 1,
  ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1,
  ['8'] = 1, ['9'] = 1,
  ['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1, ['F'] = 1, ['G'] = 1, ['H'] = 1,
  ['I'] = 1, ['J'] = 1, ['K'] = 1, ['L'] = 1, ['M'] = 1, ['N'] = 1, ['O'] = 1, ['P'] = 1,
  ['Q'] = 1, ['R'] = 1, ['S'] = 1, ['T'] = 1, ['U'] = 1, ['V'] = 1, ['W'] = 1, ['X'] = 1,
  ['Y'] = 1, ['Z'] = 1,
  ['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1, ['e'] = 1, ['f'] = 1, ['g'] = 1, ['h'] = 1,
  ['i'] = 1, ['j'] = 1, ['k'] = 1, ['l'] = 1, ['m'] = 1, ['n'] = 1, ['o'] = 1, ['p'] = 1,
  ['q'] = 1, ['r'] = 1, ['s'] = 1, ['t'] = 1, ['u'] = 1, ['v'] = 1, ['w'] = 1, ['x'] = 1,
  ['y'] = 1, ['z'] = 1,
};

static inline int __is_value_char(uint8_t c)
{
  // field-vchar, SP, HTAB and obs-text; no other control characters (including NUL)
  return c == '\t' || (c >= 0x20 && c != 0x7f);
}

static inline uint8_t __lower(uint8_t c)
{
  return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

static inline http_span_t __span(size_t off, size_t len)
{
  http_span_t s = { (uint32_t)off, (uint32_t)len };
  return s;
}

int http_span_equals(const char *buf, http_span_t span, const char *literal)
{
  size_t i = 0;
  for (; literal[i] != '\0'; i++) {
    if (i >= span.len || __lower((uint8_t)buf[span.off + i]) != __lower((uint8_t)literal[i])) {
      return 0;
    }
  }
  return i == span.len;
}

const http_header_t *http_find_header(const char *buf, const http_header_t *headers,
  size_t num_headers, const char *name)
{
  for (size_t i = 0; i < num_headers; i++) {
    if (http_span_equals(buf, headers[i].name, name)) {
      return &headers[i];
    }
  }
  return NULL;
}

// expects "\r\n" at *pos; returns 1 and advances, 0 if incomplete, -1 if malformed
static int __expect_crlf(const char *buf, size_t len, size_t *pos)
{
  if (*pos >= len) return 0;
  if (buf[*pos] != '\r') return -1;
  if (*pos + 1 >= len) return 0;
  if (buf[*pos + 1] != '\n') return -1;
  *pos += 2;
  return 1;
}

// "HTTP/1.<d>" at *pos
static int __parse_version(const char *buf, size_t len, size_t *pos, int *minor)
{
  static const char prefix[] = "HTTP/1.";
  for (size_t i = 0; i < sizeof(prefix) - 1; i++) {
    if (*pos + i >= len) return HTTP_PARSE_INCOMPLETE;
    if (buf[*pos + i] != prefix[i]) return HTTP_PARSE_ERROR;
  }
  *pos += sizeof(prefix) - 1;
  if (*pos >= len) return HTTP_PARSE_INCOMPLETE;
  if (buf[*pos] != '0' && buf[*pos] != '1') return HTTP_PARSE_ERROR;
  *minor = buf[*pos] - '0';
  (*pos)++;
  return 0;
}

// header fields up to and including the empty line; returns the offset after it
static int __parse_headers(const char *buf, size_t len, size_t pos,
  http_header_t *headers, size_t max_headers, size_t *num_headers)
{
  *num_headers = 0;

  for (;;) {
    if (pos >= len) return HTTP_PARSE_INCOMPLETE;

    if (buf[pos] == '\r') {
      int r = __expect_crlf(buf, len, &pos);
      if (r == 0) return HTTP_PARSE_INCOMPLETE;
      if (r < 0) return HTTP_PARSE_ERROR;
      if (pos > INT32_MAX) return HTTP_PARSE_ERROR;
      return (int)pos;
    }

    // obsolete line folding is a smuggling vector, so reject it
    if (buf[pos] == ' ' || buf[pos] == '\t') return HTTP_PARSE_ERROR;

    size_t name_start = pos;
    while (pos < len && __tchar[(uint8_t)buf[pos]]) pos++;
    if (pos >= len) return HTTP_PARSE_INCOMPLETE;
    if (buf[pos] != ':' || pos == name_start) return HTTP_PARSE_ERROR;
    size_t name_len = pos - name_start;
    pos++;

    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t')) pos++;
    size_t value_start = pos;
    while (pos < len && buf[pos] != '\r') {
      if (!__is_value_char((uint8_t)buf[pos])) return HTTP_PARSE_ERROR;
      pos++;
    }
    size_t value_end = pos;
    int r = __expect_crlf(buf, len, &pos);
    if (r == 0) return HTTP_PARSE_INCOMPLETE;
    if (r < 0) return HTTP_PARSE_ERROR;
    while (value_end > value_start && (buf[value_end - 1] == ' ' || buf[value_end - 1] == '\t')) {
      value_end--;
    }

    if (*num_headers >= max_headers) return HTTP_PARSE_TOO_MANY;
    headers[*num_headers].name = __span(name_start, name_len);
    headers[*num_headers].value = __span(value_start, value_end - value_start);
    (*num_headers)++;
  }
}

int http_parse_request(const char *buf, size_t len, http_request_head_t *req,
  http_header_t *headers, size_t max_headers)
{
  size_t pos = 0;
  memset(req, 0, sizeof(*req));

  // RFC 9112 2.2: ignore empty lines received before the request line
  while (pos < len && (buf[pos] == '\r' || buf[pos] == '\n')) {
    if (buf[pos] == '\r') {
      int r = __expect_crlf(buf, len, &pos);
      if (r == 0) return HTTP_PARSE_INCOMPLETE;
      if (r < 0) return HTTP_PARSE_ERROR;
    } else {
      return HTTP_PARSE_ERROR;
    }
  }

  size_t method_start = pos;
  while (pos < len && __tchar[(uint8_t)buf[pos]]) pos++;
  if (pos >= len) return HTTP_PARSE_INCOMPLETE;
  if (buf[pos] != ' ' || pos == method_start) return HTTP_PARSE_ERROR;
  req->method = __span(method_start, pos - method_start);
  pos++;

  size_t target_start = pos;
  size_t query_mark = 0;
  while (pos < len && buf[pos] != ' ') {
    uint8_t c = (uint8_t)buf[pos];
    if (c <= 0x20 || c >= 0x7f) return HTTP_PARSE_ERROR;
    if (c == '?' && query_mark == 0) query_mark = pos;
    pos++;
  }
  if (pos >= len) return HTTP_PARSE_INCOMPLETE;
  if (pos == target_start) return HTTP_PARSE_ERROR;
  req->target = __span(target_start, pos - target_start);
  pos++;

  // the path starts at the first '/', which also skips "scheme://authority" in absolute-form
  size_t path_start = target_start;
  size_t path_end = query_mark != 0 ? query_mark : pos - 1;
  if (buf[target_start] != '/' && buf[target_start] != '*') {
    size_t i = target_start;
    while (i + 2 < path_end && !(buf[i] == ':' && buf[i + 1] == '/' && buf[i + 2] == '/')) i++;
    if (i + 2 >= path_end) return HTTP_PARSE_ERROR;
    i += 3;
    while (i < path_end && buf[i] != '/') i++;
    path_start = i;
  }
  req->path = __span(path_start, path_end - path_start);
  if (query_mark != 0) {
    req->query = __span(query_mark + 1, pos - 1 - (query_mark + 1));
  } else {
    req->query = __span(pos - 1, 0);
  }

  int r = __parse_version(buf, len, &pos, &req->minor_version);
  if (r < 0) return r;
  r = __expect_crlf(buf, len, &pos);
  if (r == 0) return HTTP_PARSE_INCOMPLETE;
  if (r < 0) return HTTP_PARSE_ERROR;

  return __parse_headers(buf, len, pos, headers, max_headers, &req->num_headers);
}

int http_parse_response(const char *buf, size_t len, http_response_head_t *resp,
  http_header_t *headers, size_t max_headers)
{
  size_t pos = 0;
  memset(resp, 0, sizeof(*resp));

  int r = __parse_version(buf, len, &pos, &resp->minor_version);
  if (r < 0) return r;
  if (pos >= len) return HTTP_PARSE_INCOMPLETE;
  if (buf[pos] != ' ') return HTTP_PARSE_ERROR;
  pos++;

  int status = 0;
  for (int i = 0; i < 3; i++, pos++) {
    if (pos >= len) return HTTP_PARSE_INCOMPLETE;
    if (buf[pos] < '0' || buf[pos] > '9') return HTTP_PARSE_ERROR;
    status = status * 10 + (buf[pos] - '0');
  }
  resp->status = status;

  if (pos >= len) return HTTP_PARSE_INCOMPLETE;
  size_t reason_start = pos;
  if (buf[pos] == ' ') {
    pos++;
    reason_start = pos;
    while (pos < len && buf[pos] != '\r') {
      if (!__is_value_char((uint8_t)buf[pos])) return HTTP_PARSE_ERROR;
      pos++;
    }
  }
  resp->reason = __span(reason_start, pos - reason_start);
  r = __expect_crlf(buf, len, &pos);
  if (r == 0) return HTTP_PARSE_INCOMPLETE;
  if (r < 0) return HTTP_PARSE_ERROR;

  return __parse_headers(buf, len, pos, headers, max_headers, &resp->num_headers);
}

// comma-separated list in a span contains `token` (case-insensitive)
static int __list_contains(const char *buf, http_span_t list, const char *token)
{
  size_t i = 0;
  while (i < list.len) {
    while (i < list.len && (buf[list.off + i] == ' ' || buf[list.off + i] == '\t' || buf[list.off + i] == ',')) i++;
    size_t start = i;
    while (i < list.len && buf[list.off + i] != ',') i++;
    size_t end = i;
    while (end > start && (buf[list.off + end - 1] == ' ' || buf[list.off + end - 1] == '\t')) end--;
    if (end > start && http_span_equals(buf, __span(list.off + start, end - start), token)) {
      return 1;
    }
  }
  return 0;
}

static int __parse_length(const char *buf, http_span_t value, uint64_t *out)
{
  if (value.len == 0 || value.len > 15) return HTTP_PARSE_ERROR;
  uint64_t n = 0;
  for (uint32_t i = 0; i < value.len; i++) {
    char c = buf[value.off + i];
    if (c < '0' || c > '9') return HTTP_PARSE_ERROR;
    n = n * 10 + (uint64_t)(c - '0');
  }
  *out = n;
  return 0;
}

// Content-Length / Transfer-Encoding / Connection, shared by requests and responses
static int __framing(const char *buf, const http_header_t *headers, size_t num_headers,
  int minor_version, http_framing_t *framing)
{
  int content_lengths = 0;
  int transfer_encodings = 0;
  int close = 0;
  int keep_alive = 0;

  memset(framing, 0, sizeof(*framing));

  for (size_t i = 0; i < num_headers; i++) {
    const http_header_t *h = &headers[i];
    if (http_span_equals(buf, h->name, "content-length")) {
      if (++content_lengths > 1) return HTTP_PARSE_ERROR;
      if (__parse_length(buf, h->value, &framing->content_length) < 0) return HTTP_PARSE_ERROR;
    } else if (http_span_equals(buf, h->name, "transfer-encoding")) {
      if (++transfer_encodings > 1) return HTTP_PARSE_ERROR;
      if (!http_span_equals(buf, h->value, "chunked")) return HTTP_PARSE_ERROR;
    } else if (http_span_equals(buf, h->name, "connection")) {
      close |= __list_contains(buf, h->value, "close");
      keep_alive |= __list_contains(buf, h->value, "keep-alive");
    }
  }

  if (content_lengths > 0 && transfer_encodings > 0) return HTTP_PARSE_ERROR;

  if (transfer_encodings > 0) {
    if (minor_version == 0) return HTTP_PARSE_ERROR;
    framing->kind = HTTP_BODY_CHUNKED;
  } else if (content_lengths > 0) {
    framing->kind = framing->content_length > 0 ? HTTP_BODY_LENGTH : HTTP_BODY_NONE;
  }

  framing->keep_alive = minor_version >= 1 ? !close : (keep_alive && !close);
  return 0;
}

int http_request_framing(const char *buf, const http_header_t *headers, size_t num_headers,
  int minor_version, http_framing_t *framing)
{
  return __framing(buf, headers, num_headers, minor_version, framing);
}

int http_response_framing(const char *buf, const http_header_t *headers, size_t num_headers,
  int minor_version, int status, int is_head_request, http_framing_t *framing)
{
  if (__framing(buf, headers, num_headers, minor_version, framing) < 0) return HTTP_PARSE_ERROR;

  if (is_head_request || (status >= 100 && status < 200) || status == 204 || status == 304) {
    framing->kind = HTTP_BODY_NONE;
    framing->content_length = 0;
    return 0;
  }

  int has_length = http_find_header(buf, headers, num_headers, "content-length") != NULL;
  if (framing->kind == HTTP_BODY_NONE && !has_length) {
    framing->kind = HTTP_BODY_UNTIL_CLOSE;
    framing->keep_alive = 0;
  }
  return 0;
}

static int __hex(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// walks a chunked body; when `out` is set, also moves the chunk data down to it
static int64_t __walk_chunked(char *buf, const char *in, size_t len, size_t *out)
{
  size_t pos = 0;

  for (;;) {
    uint64_t size = 0;
    size_t digits = 0;
    while (pos < len && __hex(in[pos]) >= 0) {
      size = size * 16 + (uint64_t)__hex(in[pos]);
      if (++digits > 8) return HTTP_PARSE_ERROR;
      pos++;
    }
    if (pos >= len) return HTTP_PARSE_INCOMPLETE;
    if (digits == 0) return HTTP_PARSE_ERROR;

    // chunk extensions are allowed and ignored
    if (in[pos] == ';') {
      while (pos < len && in[pos] != '\r') {
        if (!__is_value_char((uint8_t)in[pos])) return HTTP_PARSE_ERROR;
        pos++;
      }
    }
    int r = __expect_crlf(in, len, &pos);
    if (r == 0) return HTTP_PARSE_INCOMPLETE;
    if (r < 0) return HTTP_PARSE_ERROR;

    if (size == 0) {
      // trailer fields, then the empty line
      for (;;) {
        if (pos >= len) return HTTP_PARSE_INCOMPLETE;
        if (in[pos] == '\r') {
          r = __expect_crlf(in, len, &pos);
          if (r == 0) return HTTP_PARSE_INCOMPLETE;
          if (r < 0) return HTTP_PARSE_ERROR;
          return (int64_t)pos;
        }
        while (pos < len && in[pos] != '\r') {
          if (!__is_value_char((uint8_t)in[pos])) return HTTP_PARSE_ERROR;
          pos++;
        }
        r = __expect_crlf(in, len, &pos);
        if (r == 0) return HTTP_PARSE_INCOMPLETE;
        if (r < 0) return HTTP_PARSE_ERROR;
      }
    }

    if (size > len - pos) return HTTP_PARSE_INCOMPLETE;
    if (out != NULL) {
      memmove(buf + *out, in + pos, (size_t)size);
      *out += (size_t)size;
    }
    pos += (size_t)size;
    r = __expect_crlf(in, len, &pos);
    if (r == 0) return HTTP_PARSE_INCOMPLETE;
    if (r < 0) return HTTP_PARSE_ERROR;
  }
}

int64_t http_chunked_length(const char *buf, size_t len)
{
  return __walk_chunked(NULL, buf, len, NULL);
}

size_t http_chunked_decode(char *buf, size_t len)
{
  // the decoded data is never longer than what precedes it, so moving it down is safe
  size_t out = 0;
  __walk_chunked(buf, buf, len, &out);
  return out;
}
