#ifndef ISERE_JS_H_

#include "isere.h"

#include "quickjs.h"

#define ISERE_JS_H_

#define ISERE_JS_LOG_TAG "js"
#define ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME "__response"

#define ISERE_JS_RESPONSE_IS_BASE64_ENCODED_PROP_NAME "isBase64Encoded"
#define ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME "statusCode"
#define ISERE_JS_RESPONSE_HEADERS_PROP_NAME "headers"
#define ISERE_JS_RESPONSE_BODY_PROP_NAME "body"

// TODO: make this configurable
#define ISERE_JS_STACK_SIZE 65536
#define ISERE_JS_CONSOLE_LOG_BUFFER_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

int js_init(isere_t *isere, isere_js_t *js);
int js_deinit(isere_js_t *js);
int js_eval(isere_js_t *js);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_JS_H_ */
