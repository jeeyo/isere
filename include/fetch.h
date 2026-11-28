#ifndef ISERE_FETCH_H_
#define ISERE_FETCH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "engine.h"
#include "http_parser.h"

// Outbound HTTP and HTTPS for the JS fetch() polyfill. Requests run on the event
// loop like everything else, so a handler can fetch from the server it runs on
// (the JWKS endpoint on the same board) without blocking it.

typedef struct isere_fetch_result_s {
  const char *error;              // NULL on success
  int status;
  const char *buf;                // response head and body
  const http_header_t *headers;
  size_t num_headers;
  const char *body;
  size_t body_len;
} isere_fetch_result_t;

typedef struct {
  const char *method;
  size_t method_len;
  const char *url;
  size_t url_len;
  const char *const *header_names;   // flat lists of num_headers entries
  const size_t *header_name_lens;
  const char *const *header_values;
  const size_t *header_value_lens;
  size_t num_headers;
  const char *body;
  size_t body_len;
} isere_fetch_request_t;

void isere_fetch_init(void);
void isere_fetch_deinit(void);

// Starts a fetch for the running JS code; `fn` gets the result through
// js_vm_fetch_done(). Returns 0, or <0 if it could not start (fn is released).
int isere_fetch_start(js_vm_t *vm, const isere_fetch_request_t *request, js_ref_t fn);

// drop fetches started by a request or owned by an instance; their callbacks never run
void isere_fetch_cancel_request(uint32_t request_id);
void isere_fetch_cancel_vm(js_vm_t *vm);

// local TCP port of an outbound connection, so httpd can recognise connections
// this server opened to itself
int isere_fetch_is_own_port(uint16_t port);

// called by fetch when a request finishes (src/engine.c)
void isere_engine_fetch_done(js_vm_t *vm, uint32_t request_id, js_ref_t fn, const isere_fetch_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_FETCH_H_ */
