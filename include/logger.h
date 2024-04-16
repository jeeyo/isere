#ifndef ISERE_LOGGER_H_
#define ISERE_LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"

#include "FreeRTOS.h"
#include "semphr.h"

int isere_logger_init(isere_t *isere, isere_logger_t *logger);
void isere_logger_deinit(isere_logger_t *logger);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOGGER_H_ */
