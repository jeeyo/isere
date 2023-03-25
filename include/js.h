#ifndef ISERE_JS_H_

#include "isere.h"

#include "quickjs.h"

#define ISERE_JS_H_

#define ISERE_JS_LOG_TAG "js"

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
