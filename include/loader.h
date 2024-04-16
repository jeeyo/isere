#ifndef ISERE_LOADER_H_
#define ISERE_LOADER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"
#include "quickjs.h"

#define ISERE_LOADER_LOG_TAG "loader"

#ifdef ISERE_USE_DYNLINK
#define ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH "handler.so"
#define ISERE_LOADER_HANDLER_FUNCTION "handler"
#define ISERE_LOADER_HANDLER_SIZE_FUNCTION "handler_len"
#else
extern unsigned char handler[];
extern unsigned int handler_len;
#endif /* ISERE_USE_DYNLINK */

int isere_loader_init(isere_t *isere, isere_loader_t *loader);
int isere_loader_deinit(isere_loader_t *loader);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOADER_H_ */
