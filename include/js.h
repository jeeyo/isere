#ifndef ISERE_JS_H_
#define ISERE_JS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "FreeRTOS.h"
#include "croutine.h"

#define ISERE_JS_LOG_TAG "js"

int isere_js_init(isere_t *isere, isere_js_t *js);
int isere_js_deinit(isere_js_t *js);
int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx);
int isere_js_free_context(isere_js_t *js, isere_js_context_t *ctx);
int isere_js_eval(
  isere_js_context_t *ctx,
  unsigned char *handler,
  unsigned int handler_len,
  const char *method,
  const char *path,
  const char *query,
  const char (*request_header_names)[],
  const char (*request_header_values)[],
  const uint32_t request_headers_len,
  const char *body);
int isere_js_poll(isere_js_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_JS_H_ */
