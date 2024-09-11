#ifndef ISERE_RUNTIME_H_
#define ISERE_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "js.h"
#include "httpd.h"

int js_runtime_init(isere_js_t *js);
int js_runtime_deinit(isere_js_t *js);

int js_runtime_init_context(isere_js_t *js, isere_js_context_t *ctx);
int js_runtime_deinit_context(isere_js_t *js, isere_js_context_t *ctx);

int js_runtime_process_response(isere_js_context_t *ctx, httpd_response_object_t *resp);
void js_runtime_populate_params_to_globalobj(
  isere_js_context_t *ctx,
  const char *method,
  const char *path,
  const char *query,
  const httpd_header_t *request_headers,
  const uint32_t request_headers_len,
  const char *body);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_RUNTIME_H_ */
