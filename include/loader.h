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
void *loader_open(const char *filename);
int loader_close(void *dl);
uint8_t *loader_get_fn(void *dl, uint32_t *size);
char *loader_last_error(void *dl);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOADER_H_ */
