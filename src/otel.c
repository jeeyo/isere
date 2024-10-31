#include "otel.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <errno.h>

#include "libuv/uv.h"

#include "FreeRTOS.h"
#include "task.h"

#include "queue.h"

static isere_t *__isere = NULL;
static isere_otel_t *__otel = NULL;

static otel_metrics_counter_t *__counters_head = NULL;

static int connect_to_otel();
static inline void disconnect_from_otel();

static void __otel_task(void *param);

static void __on_connected(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  uv__io_stop(&__otel->loop, w, UV_POLLOUT);

  __isere->logger->info(ISERE_OTEL_LOG_TAG, "Connected to OpenTelemetry Collector");
  __otel->last_connect_attempt = 0;
}

#define OTEL_RESOURCE_OBJ "\"resource\":{\
\"attribute\":[\
{\"key\":\"service.name\",\"value\":{\"stringValue\":\"%s\"}},\
{\"key\":\"service.version\",\"value\":{\"stringValue\":\"%s\"}},\
{\"key\":\"runtime.name\",\"value\":{\"stringValue\":\"%s\"}}\
]\
}"

// TODO: Only support Cumulative Counter for now
// See "AggregationTemporality" in https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/metrics/v1/metrics.proto
#define OTEL_METRIC_COUNTER_OBJ "{\
\"name\":\"%s\",\
\"unit\":\"%s\",\
\"description\":\"%s\",\
\"sum\":{\
\"aggregationTemporality\":2,\
\"isMonotonic\":true,\
\"dataPoints\":[\
{\"asInt\":%d,\"startTimeUnixNano\":\"1544712660300000000\",\"timeUnixNano\":\"1544712660300000000\",\"attributes\":[]}\
]\
}\
}"

static int connect_to_otel()
{
  if ((__otel->fd = isere_tcp_socket_new()) < 0) {
    return -1;
  }

  if (isere_tcp_socket_set_nonblock(__otel->fd) < 0) {
    return -1;
  }

  printf(
    "{"
      "\"resourceMetrics\": ["
        "{"
          OTEL_RESOURCE_OBJ
          ","
          "\"scopeMetrics\": ["
            "{"
              "\"scope\": {"
              "},"
              "\"metrics\": ["
                OTEL_METRIC_COUNTER_OBJ
              "]"
            "}"
          "]"
        "}"
      "]"
    "}",
  ISERE_APP_NAME, ISERE_APP_VERSION, "jerryscript",
  "my.counter", "1", "my.counter", 25);

  __isere->logger->info(ISERE_OTEL_LOG_TAG, "Connecting to OpenTelemetry Collector %s:%d", ISERE_OTEL_HOST, ISERE_OTEL_PORT);
  __otel->last_connect_attempt = xTaskGetTickCount();

  int ret = isere_tcp_connect(__otel->fd, ISERE_OTEL_HOST, ISERE_OTEL_PORT);
  if (ret < 0 && ret != -2) {
    __isere->logger->error(ISERE_OTEL_LOG_TAG, "Unable to connect to OpenTelemetry Collector", ISERE_OTEL_HOST, ISERE_OTEL_PORT);
    return -1;
  }

  uv__io_t *w = &__otel->w;
  w->fd = __otel->fd;
  w->events = 0;
  w->cb = __on_connected;
  w->opaque = NULL;
  uv__queue_init(&w->watcher_queue);

  uv__io_start(&__otel->loop, w, UV_POLLOUT);

  return 0;
}

static inline void disconnect_from_otel()
{
  __otel->last_connect_attempt = 0;
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLOUT);
  isere_tcp_close(__otel->fd);
}

int isere_otel_init(isere_t *isere, isere_otel_t *otel)
{
  __isere = isere;
  __otel = otel;

  if (isere->logger == NULL) {
    return -1;
  }

  otel->fd = -1;

  uv__platform_loop_init(&otel->loop);
  otel->loop.nfds = 0;
  otel->loop.watchers = NULL;
  otel->loop.nwatchers = 0;
  uv__queue_init(&otel->loop.watcher_queue);

  // start otel task
  if (xTaskCreate(__otel_task, "otel", ISERE_OTEL_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &otel->tsk) != pdPASS) {
    isere->logger->error(ISERE_OTEL_LOG_TAG, "Unable to create otel task");
    return -1;
  }

  return 0;
}

int isere_otel_deinit(isere_otel_t *otel)
{
  if (__isere) {
    __isere->should_exit = 1;
    __isere = NULL;
  }

  if (__otel) {
    __otel = NULL;
  }

  uv__platform_loop_delete(&otel->loop);

  isere_tcp_close(otel->fd);

  return 0;
}

int isere_otel_create_counter(otel_metrics_counter_t *counter, const char *name, const char *unit)
{
  counter = (otel_metrics_counter_t *)pvPortMalloc(sizeof(otel_metrics_counter_t));
  strncpy(counter->name, name, ISERE_OTEL_METRIC_MAX_NAME_LEN);
  strncpy(counter->unit, unit, ISERE_OTEL_METRIC_MAX_UNIT_LEN);
  counter->count = 0;
  counter->next = __counters_head;
  __counters_head = counter;

  return 0;
}

int isere_otel_counter_add(otel_metrics_counter_t *counter, uint32_t increment)
{
  counter->count += increment;
  return 0;
}

static void __otel_task(void *param)
{
  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  connect_to_otel();

  while (!__isere->should_exit)
  {
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // retry when connecting takes too long
    if (__otel->last_connect_attempt != 0 &&
        xTaskGetTickCount() - __otel->last_connect_attempt > pdMS_TO_TICKS(ISERE_OTEL_CONNECT_TIMEOUT_MS))
    {
      __isere->logger->warning(ISERE_OTEL_LOG_TAG, "Connection to OpenTelemetry Collector timed out, retrying...");
      disconnect_from_otel();
      connect_to_otel();
    }
  }

exit:
  __isere->logger->error(ISERE_OTEL_LOG_TAG, "otel task was unexpectedly closed");
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLOUT);
  __isere->should_exit = 1;
  vTaskDelete(NULL);
}
