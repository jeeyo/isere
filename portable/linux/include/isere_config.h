#ifndef ISERE_CONFIG_H_
#define ISERE_CONFIG_H_

#include "FreeRTOSConfig.h"

#define ISERE_TCP_MAX_CONNECTIONS                 12

// in words (8 bytes in the POSIX port); PORTING.md (FreeRTOS configuration) has the measurement
#ifndef ISERE_HTTPD_TASK_STACK_SIZE
#define ISERE_HTTPD_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE
#endif

#endif /* ISERE_CONFIG_H_ */
