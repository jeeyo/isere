#ifndef ISERE_RUNTIME_H_
#define ISERE_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "js.h"
#include "httpd.h"

int js_runtime_process_response(isere_js_context_t *ctx, httpd_response_object_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_RUNTIME_H_ */
