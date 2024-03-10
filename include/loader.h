#ifndef ISERE_LOADER_H_

#define ISERE_LOADER_H_

#include "isere.h"
#include "quickjs.h"

#define ISERE_LOADER_LOG_TAG "loader"

#define ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH "handler.so"
#define ISERE_LOADER_HANDLER_FUNCTION "handler"
#define ISERE_LOADER_HANDLER_SIZE_FUNCTION "handler_len"

#ifdef __cplusplus
extern "C" {
#endif

int loader_init(isere_t *isere, isere_loader_t *loader, const char *dll_path);
int loader_deinit(isere_loader_t *loader);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOADER_H_ */
