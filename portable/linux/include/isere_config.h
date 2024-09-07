#ifndef ISERE_CONFIG_H_
#define ISERE_CONFIG_H_

#include "FreeRTOSConfig.h"

#define ISERE_TCP_MAX_CONNECTIONS                 12

#define ISERE_HTTPD_SERVER_TASK_STACK_SIZE        configMINIMAL_STACK_SIZE
#define ISERE_HTTPD_HTTP_HANDLER_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE

#endif /* ISERE_CONFIG_H_ */
