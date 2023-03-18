#ifndef ISERE_LOADER_H_

#define ISERE_LOADER_H_

#include "isere.h"
#include "quickjs.h"

#define ISERE_LOADER_HANDLER_FUNCTION "handler"
#define ISERE_LOADER_HANDLER_SIZE_FUNCTION "handler_len"

#ifdef __cplusplus
extern "C" {
#endif

int loader_init(isere_t *isere);
int loader_open(const char *filename);
int loader_close();
uint8_t *loader_get_fn(uint32_t *size);
char *loader_last_error();

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOADER_H_ */
