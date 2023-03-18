#ifndef ISERE_SERVER_H_

#include "isere.h"

#define ISERE_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

void server_init(void);
void logger_get_instance(isere_logger_t *logger);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_SERVER_H_ */
