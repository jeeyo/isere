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

#ifndef ISERE_OTEL_HOST
#define ISERE_OTEL_HOST "127.0.0.1"
#endif /* ISERE_OTEL_HOST */

#ifndef ISERE_OTEL_PORT
#define ISERE_OTEL_PORT 4318
#endif /* ISERE_OTEL_PORT */

#define ISERE_OTEL_CONNECT_TIMEOUT_MS 5000

#ifndef ISERE_OTEL_TASK_STACK_SIZE
#define ISERE_OTEL_TASK_STACK_SIZE  configMINIMAL_STACK_SIZE
#endif /* ISERE_OTEL_TASK_STACK_SIZE */

int isere_otel_init(isere_t *isere, isere_otel_t *otel);
int isere_otel_deinit(isere_otel_t *otel);

#define ISERE_OTEL_TX_BUF_LEN 512

#define ISERE_OTEL_METRIC_MAX_NAME_LEN 64
#define ISERE_OTEL_METRIC_MAX_UNIT_LEN 8

enum otel_metrics_type_t {
  COUNTER,
  GAUGE
};

enum otel_metrics_counter_AggregationTemporality_t {
  DELTA = 1,
  CUMULATIVE = 2
};

typedef struct otel_metrics_counter_s otel_metrics_counter_t;
struct otel_metrics_counter_s {
  char name[ISERE_OTEL_METRIC_MAX_NAME_LEN];
  char unit[ISERE_OTEL_METRIC_MAX_UNIT_LEN];
  uint32_t count;
  otel_metrics_counter_t *next;
};

typedef struct {
  enum otel_metrics_type_t type;
  void *instrument;
} otel_metrics_t;

int isere_otel_create_counter(otel_metrics_counter_t *counter, const char *name, const char *unit);
// int isere_otel_delete_counter(otel_metrics_counter_t *counter);
int isere_otel_counter_add(otel_metrics_counter_t *counter, uint32_t increment);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_OTEL_H_ */
