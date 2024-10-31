#ifndef ISERE_OTEL_H_
#define ISERE_OTEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere.h"
#include "tcp.h"

#include <stdlib.h>
#include <stdint.h>

#include "libuv/uv.h"

#include "FreeRTOS.h"
#include "task.h"

#define ISERE_OTEL_LOG_TAG "otel"

#define ISERE_OTEL_HOST "127.0.0.1"
#define ISERE_OTEL_PORT 4318

#define ISERE_OTEL_CONNECT_TIMEOUT_MS 5000

#define ISERE_OTEL_LINE_BUFFER_LEN 64

#ifndef ISERE_OTEL_TASK_STACK_SIZE
#define ISERE_OTEL_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE
#endif /* ISERE_OTEL_TASK_STACK_SIZE */

typedef struct {
  char linebuf[ISERE_OTEL_LINE_BUFFER_LEN];
  int linebuflen;
  int32_t recvd;  // total number of bytes received
  uv__io_t w;
} otel_conn_t;

int isere_otel_init(isere_t *isere, isere_otel_t *otel);
int isere_otel_deinit(isere_otel_t *otel);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_OTEL_H_ */
