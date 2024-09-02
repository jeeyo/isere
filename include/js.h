#ifndef ISERE_JS_H_
#define ISERE_JS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "FreeRTOS.h"
#include "croutine.h"

#define ISERE_JS_LOG_TAG "js"

#define ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME "__response"

#define ISERE_JS_RESPONSE_IS_BASE64_ENCODED_PROP_NAME "isBase64Encoded"
#define ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME "statusCode"
#define ISERE_JS_RESPONSE_HEADERS_PROP_NAME "headers"
#define ISERE_JS_RESPONSE_BODY_PROP_NAME "body"

// TODO: make this configurable
#define ISERE_JS_STACK_SIZE 4096

int isere_js_init(isere_t *isere, isere_js_t *js);
int isere_js_deinit(isere_js_t *js);
int isere_js_new_context(isere_js_t *js, isere_js_context_t *ctx);
int isere_js_free_context(isere_js_context_t *ctx);
int isere_js_eval(isere_js_context_t *ctx, unsigned char *handler, unsigned int handler_len);
int isere_js_poll(isere_js_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_JS_H_ */
