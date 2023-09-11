#ifndef ISERE_INI_H

#include "isere.h"

#include "config.capnp.h"

#define ISERE_INI_H

#ifdef __cplusplus
extern "C" {
#endif

int ini_init(isere_t *isere, isere_ini_t *ini);
int ini_getvalue(isere_ini_t *ini, const char *section, const char *key, char *value, size_t value_size);
void ini_deinit(isere_ini_t *ini);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_INI_H */
