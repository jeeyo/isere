#ifndef LOADER_H_

#define LOADER_H_

#include "logger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (loader_fn_t)();

void *loader_open(isere_logger_t *logger);
int loader_close(void *ctx);
loader_fn_t *loader_get_fn(void *ctx, const char *fn);
// char *loader_last_error();

#ifdef __cplusplus
}
#endif

#endif /* LOADER_H_ */
