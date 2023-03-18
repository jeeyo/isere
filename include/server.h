#ifndef ISERE_SERVER_H_

#include "isere.h"

#define ISERE_SERVER_H_

#define ISERE_SERVER_PORT 8080

#ifdef __cplusplus
extern "C" {
#endif

int server_init(void);
int server_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_SERVER_H_ */
