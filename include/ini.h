#ifndef ISERE_INI_H

#include "isere.h"

#include "config.capnp.h"

#define ISERE_INI_H

#ifdef __cplusplus
extern "C" {
#endif

#define INI_FILENAME "config.bin"
#define INI_MAX_CONFIG_FILE_SIZE 256

int ini_init(isere_t *isere, isere_ini_t *ini);
int ini_get_timeout(isere_ini_t *ini);
void ini_deinit(isere_ini_t *ini);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_INI_H */
