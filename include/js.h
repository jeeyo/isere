#ifndef ISERE_JS_H_

#include "isere.h"

#include "quickjs.h"

#define ISERE_JS_H_

// TODO: make this configurable
#define ISERE_JS_STACK_SIZE 65536

#define ISERE_JS_LOG_BUFFER_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

typedef JSContext js_ctx_t;

js_ctx_t *js_init(isere_t *isere);
int js_deinit(js_ctx_t *ctx);
int js_eval(js_ctx_t *ctx, uint8_t *fn, uint32_t fn_len);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_JS_H_ */
