#ifndef LOADER_H_

#define LOADER_H_

#include "isere.h"
#include "quickjs.h"

#define ISERE_LOADER_HANDLER_FUNCTION "handler"
#define ISERE_LOADER_HANDLER_SIZE_FUNCTION "handler_len"

// TODO: make this configurable
#define ISERE_LOADER_STACK_SIZE 65536

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t loader_fn_t;

int loader_init(isere_t *isere);
int loader_open(const char *filename);
int loader_close();
loader_fn_t *loader_get_fn(uint32_t *size);
int loader_eval_fn(loader_fn_t *fn, uint32_t fn_size);
char *loader_last_error();

#ifdef __cplusplus
}
#endif

#endif /* LOADER_H_ */
