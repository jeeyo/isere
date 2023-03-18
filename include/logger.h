#ifndef ISERE_LOGGER_H_

#include "isere.h"

#define ISERE_LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

void logger_init(void);
void logger_get_instance(isere_logger_t *logger);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOGGER_H_ */
