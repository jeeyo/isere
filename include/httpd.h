#ifndef ISERE_HTTPD_H_
#define ISERE_HTTPD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "logger.h"

#include "FreeRTOS.h"
#include "task.h"

#define ISERE_HTTPD_LOG_TAG "httpd"
#define ISERE_HTTPD_PORT 8080

// Each connection receives straight into its own buffer, which starts small and
// grows up to two budgets: one for the request head and one for the body.
// Exceeding them answers 431 and 413. Both can be overridden on Linux with
// ISERE_HEAD_BUDGET and ISERE_BODY_BUDGET.
#ifndef ISERE_HTTPD_HEAD_BUDGET
#define ISERE_HTTPD_HEAD_BUDGET (8 * 1024)
#endif
#ifndef ISERE_HTTPD_BODY_BUDGET
#define ISERE_HTTPD_BODY_BUDGET (16 * 1024)
#endif
#ifndef ISERE_HTTPD_INITIAL_BUFFER
#define ISERE_HTTPD_INITIAL_BUFFER 512
#endif

// an idle keep-alive connection is closed after this long
#ifndef ISERE_HTTPD_IDLE_TIMEOUT_MS
#define ISERE_HTTPD_IDLE_TIMEOUT_MS 5000
#endif
// a request head must arrive within this long of its first byte, or 408
#ifndef ISERE_HTTPD_HEAD_TIMEOUT_MS
#define ISERE_HTTPD_HEAD_TIMEOUT_MS 10000
#endif

// connections the server opens to itself (fetch to its own endpoints) may go
// over the connection limit by this many, so a full pool can't deadlock them
#ifndef ISERE_HTTPD_SELF_RESERVE
#define ISERE_HTTPD_SELF_RESERVE 2
#endif

#ifndef ISERE_HTTPD_TASK_STACK_SIZE
#define ISERE_HTTPD_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE
#endif /* ISERE_HTTPD_TASK_STACK_SIZE */

typedef struct {
  volatile int should_exit;
  TaskHandle_t tsk;
  isere_logger_t *logger;
} isere_httpd_t;

int isere_httpd_init(isere_httpd_t *httpd, isere_logger_t *logger);
int isere_httpd_deinit(isere_httpd_t *httpd);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_HTTPD_H_ */
