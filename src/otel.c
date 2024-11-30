#include "otel.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/param.h>
#include <errno.h>

#include "libuv/uv.h"

#include "pb_encode.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"

#include "FreeRTOS.h"
#include "task.h"

#include "queue.h"

#include "isere.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

static isere_otel_t *__otel = NULL;

static char __tx_buf[ISERE_OTEL_TX_BUF_LEN] = {0};
static otel_metrics_counter_t *__counters_head = NULL;

static int __connect_to_otel();
static inline void __disconnect_from_otel();

static void __otel_task(void *param);

static void __on_connected(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  uv__io_stop(&__otel->loop, w, UV_POLLOUT);

  __otel->logger->info(ISERE_OTEL_LOG_TAG, "Connected to OpenTelemetry Collector");
  __otel->last_connect_attempt = 0;
  __otel->last_sent = xTaskGetTickCount();
}

static int __connect_to_otel()
{
  if ((__otel->fd = isere_tcp_socket_new()) < 0) {
    return -1;
  }

  if (isere_tcp_socket_set_nonblock(__otel->fd) < 0) {
    return -1;
  }

  __otel->logger->info(ISERE_OTEL_LOG_TAG, "Connecting to OpenTelemetry Collector %s:%d", ISERE_OTEL_HOST, ISERE_OTEL_PORT);
  __otel->last_connect_attempt = xTaskGetTickCount();

  int ret = isere_tcp_connect(__otel->fd, ISERE_OTEL_HOST, ISERE_OTEL_PORT);
  if (ret < 0 && ret != -2) {
    __otel->logger->error(ISERE_OTEL_LOG_TAG, "Unable to connect to OpenTelemetry Collector", ISERE_OTEL_HOST, ISERE_OTEL_PORT);
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

static inline void __disconnect_from_otel()
{
  __otel->last_connect_attempt = 0;
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLOUT);
  isere_tcp_close(__otel->fd);
}

int isere_otel_init(isere_otel_t *otel, isere_logger_t *logger, isere_rtc_t *rtc)
{
  otel->logger = logger;
  otel->rtc = rtc;
  otel->should_exit = 0;
  otel->fd = -1;

  __otel = otel;

  uv__platform_loop_init(&otel->loop);
  otel->loop.nfds = 0;
  otel->loop.watchers = NULL;
  otel->loop.nwatchers = 0;
  uv__queue_init(&otel->loop.watcher_queue);

  // start otel task
  if (xTaskCreate(__otel_task, "otel", ISERE_OTEL_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, &otel->tsk) != pdPASS) {
    logger->error(ISERE_OTEL_LOG_TAG, "Unable to create otel task");
    return -1;
  }

  return 0;
}

int isere_otel_deinit(isere_otel_t *otel)
{
  if (__otel) {
    __otel = NULL;
  }

  uv__platform_loop_delete(&otel->loop);

  isere_tcp_close(otel->fd);

  return 0;
}

int isere_otel_create_counter(
  const char *name,
  const char *description,
  const char *unit,
  enum otel_metrics_counter_aggregation_temporality_t aggregation,
  otel_metrics_counter_t **counter)
{
  otel_metrics_counter_t *c = (otel_metrics_counter_t *)pvPortMalloc(sizeof(otel_metrics_counter_t));

  strncpy(c->name, name, ISERE_OTEL_METRIC_MAX_NAME_LEN);
  strncpy(c->description, description, ISERE_OTEL_METRIC_MAX_DESCRIPTION_LEN);
  strncpy(c->unit, unit, ISERE_OTEL_METRIC_MAX_UNIT_LEN);
  c->count = 0;
  c->aggregation = aggregation;
  c->next = __counters_head;
  __counters_head = c;

  *counter = c;

  return 0;
}

int isere_otel_counter_add(otel_metrics_counter_t *counter, uint32_t increment)
{
  counter->count += increment;
  return 0;
}

static int __send_chunk(const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);

  int ret = -1;
  int tx_len = 0;

  char chunklen[8] = {0};
  int chunklen_len = 0;

  tx_len = vsnprintf(__tx_buf, ISERE_OTEL_TX_BUF_LEN, fmt, vargs);

  va_end(vargs);

  chunklen_len = snprintf(chunklen, 8, "%d\r\n", tx_len);
  ret = isere_tcp_write(__otel->fd, chunklen, chunklen_len);
  if (ret < 0) {
    return -1;
  }

  ret = isere_tcp_write(__otel->fd, __tx_buf, tx_len);
  if (ret < 0) {
    return -1;
  }

  ret = isere_tcp_write(__otel->fd, "\r\n", 2);
  if (ret < 0) {
    return -1;
  }

  return 0;
}

static bool encode_string(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (uint8_t *)(*arg), strlen(*arg));
}

struct otel_kv_t {
  const char *key;
  const char *value;
};

static bool encode_resource_attributes_kv(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  struct otel_kv_t otel_resource_attrs[] = {
    {"service.name", ISERE_APP_NAME},
    {"service.version", ISERE_APP_VERSION},
    {"runtime.name", ISERE_RUNTIME_NAME},
  };

  opentelemetry_proto_common_v1_KeyValue kv = {};

  for (int i = 0; i < COUNT_OF(otel_resource_attrs); i++)
  {
    char *key = strdup(otel_resource_attrs[i].key);
    char *value = strdup(otel_resource_attrs[i].value);

    kv.key.funcs.encode = &encode_string;
    kv.key.arg = key;

    kv.has_value = true;
    kv.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
    kv.value.value.string_value.funcs.encode = &encode_string;
    kv.value.value.string_value.arg = value;

    if (!pb_encode_tag_for_field(stream, field)) {
      vPortFree(key);
      vPortFree(value);
      return false;
    }

    if (!pb_encode_submessage(stream, opentelemetry_proto_common_v1_KeyValue_fields, &kv)) {
      vPortFree(key);
      vPortFree(value);
      return false;
    }
  }

  return true;
}

static bool encode_number_data_point(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  otel_metrics_counter_t *counter = (otel_metrics_counter_t *)*arg;

  opentelemetry_proto_metrics_v1_NumberDataPoint data_point = {};
  data_point.which_value = opentelemetry_proto_metrics_v1_NumberDataPoint_as_int_tag;
  data_point.value.as_int = counter->count;

  data_point.start_time_unix_nano = __otel->start_time * 1000 * 1000 * 1000;

  uint64_t timestamp = isere_rtc_get_unix_timestamp(__otel->rtc);
  data_point.time_unix_nano = timestamp * 1000 * 1000 * 1000;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_submessage(stream, opentelemetry_proto_metrics_v1_NumberDataPoint_fields, &data_point);
}

static bool encode_metrics(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  otel_metrics_counter_t *current_counter = __counters_head;

  while (current_counter != NULL)
  {
    opentelemetry_proto_metrics_v1_Metric metric = {};
    metric.name.funcs.encode = &encode_string;
    metric.name.arg = current_counter->name;
    metric.description.funcs.encode = &encode_string;
    metric.description.arg = current_counter->description;
    metric.unit.funcs.encode = &encode_string;
    metric.unit.arg = current_counter->unit;

    metric.which_data = opentelemetry_proto_metrics_v1_Metric_sum_tag;
    metric.data.sum.data_points.funcs.encode = &encode_number_data_point;
    metric.data.sum.data_points.arg = current_counter;
    metric.data.sum.is_monotonic = true;
    metric.data.sum.aggregation_temporality =
      current_counter->aggregation == DELTA
      ? opentelemetry_proto_metrics_v1_AggregationTemporality_AGGREGATION_TEMPORALITY_DELTA
      : opentelemetry_proto_metrics_v1_AggregationTemporality_AGGREGATION_TEMPORALITY_CUMULATIVE;

    if (!pb_encode_tag_for_field(stream, field))
      return false;

    if (!pb_encode_submessage(stream, opentelemetry_proto_metrics_v1_Metric_fields, &metric))
      return false;

next:
    current_counter = current_counter->next;
  }

  return true;
}

static bool encode_scope_metrics(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
  opentelemetry_proto_metrics_v1_ScopeMetrics scope_metrics = {};
  scope_metrics.has_scope = false;
  scope_metrics.metrics.arg = NULL;
  scope_metrics.metrics.funcs.encode = &encode_metrics;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_submessage(stream, opentelemetry_proto_metrics_v1_ScopeMetrics_fields, &scope_metrics);
}

static bool encode_resource_metrics(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  opentelemetry_proto_metrics_v1_ResourceMetrics *resource_metrics =
      (opentelemetry_proto_metrics_v1_ResourceMetrics *)*arg;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_submessage(
    stream, opentelemetry_proto_metrics_v1_ResourceMetrics_fields,
    resource_metrics);
}

static void __otel_task(void *param)
{
  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  __connect_to_otel();

  __otel->start_time = isere_rtc_get_unix_timestamp(__otel->rtc);

  while (!__otel->should_exit)
  {
    vTaskDelay(pdMS_TO_TICKS(100));

    // connecting
    if (__otel->last_connect_attempt != 0)
    {
      // poll sockets to see if there's any new events
      uv__io_poll(&__otel->loop, 0);

      // retry when connecting takes too long
      if (xTaskGetTickCount() - __otel->last_connect_attempt > pdMS_TO_TICKS(ISERE_OTEL_CONNECT_TIMEOUT_MS))
      {
        __otel->logger->warning(ISERE_OTEL_LOG_TAG, "Connection to OpenTelemetry Collector timed out, retrying...");
        __disconnect_from_otel();
        __connect_to_otel();
      }

      continue;
    }

    // send on interval
    if (xTaskGetTickCount() - __otel->last_sent > pdMS_TO_TICKS(ISERE_OTEL_SEND_INTERVAL_MS))
    {
      int ret = -1;
      int tx_len = 0;

      pb_ostream_t stream = pb_ostream_from_buffer(__tx_buf, ISERE_OTEL_TX_BUF_LEN);

      opentelemetry_proto_metrics_v1_MetricsData metrics_data =
        opentelemetry_proto_metrics_v1_MetricsData_init_zero;
      opentelemetry_proto_metrics_v1_ResourceMetrics resource_metrics =
        opentelemetry_proto_metrics_v1_ResourceMetrics_init_zero;
      opentelemetry_proto_metrics_v1_ScopeMetrics scope_metrics =
          opentelemetry_proto_metrics_v1_ScopeMetrics_init_zero;

      opentelemetry_proto_metrics_v1_Metric **metrics;
      opentelemetry_proto_common_v1_KeyValue *
          *resource_attributes_key_values = NULL;

      resource_metrics.has_resource = true;
      resource_metrics.resource.attributes.funcs.encode = &encode_resource_attributes_kv;

      // if (__counters_head != NULL) {
      resource_metrics.scope_metrics.funcs.encode = &encode_scope_metrics;
      resource_metrics.scope_metrics.arg = &scope_metrics;

      metrics_data.resource_metrics.funcs.encode = &encode_resource_metrics;
      metrics_data.resource_metrics.arg = &resource_metrics;
      // }

      if (!pb_encode(&stream, opentelemetry_proto_metrics_v1_MetricsData_fields, &metrics_data)) {
        __otel->logger->error(ISERE_OTEL_LOG_TAG, "Encoding failed: %s\n", PB_GET_ERROR(&stream));
        continue;
      }

      const char *metrics_http_header1 = 
        "POST /v1/metrics HTTP/1.1\r\n"
        "Host: "ISERE_OTEL_HOST":"STR(ISERE_OTEL_PORT)"\r\n"
        "Content-Type: application/x-protobuf\r\n"
        "Content-Length: ";
      ret = isere_tcp_write(__otel->fd, metrics_http_header1, strlen(metrics_http_header1));
      if (ret < 0)
      {
        __disconnect_from_otel();
        __connect_to_otel();
        continue;
      }

      char content_length[8];
      ret = snprintf(content_length, 8, "%lu", stream.bytes_written);
      ret = isere_tcp_write(__otel->fd, content_length, ret);
      if (ret < 0)
      {
        __disconnect_from_otel();
        __connect_to_otel();
        continue;
      }

      const char *metrics_http_header2 =
        "\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: "ISERE_APP_NAME"/"ISERE_APP_VERSION"\r\n"
        "\r\n";
      ret = isere_tcp_write(__otel->fd, metrics_http_header2, strlen(metrics_http_header2));
      if (ret < 0)
      {
        __disconnect_from_otel();
        __connect_to_otel();
        continue;
      }

      ret = isere_tcp_write(__otel->fd, __tx_buf, stream.bytes_written);
      if (ret < 0)
      {
        __disconnect_from_otel();
        __connect_to_otel();
        continue;
      }

      ret = isere_tcp_write(__otel->fd, "\r\n", 2);
      if (ret < 0)
      {
        __disconnect_from_otel();
        __connect_to_otel();
        continue;
      }

      __otel->last_sent = xTaskGetTickCount();
    }
  }

exit:
  __otel->logger->error(ISERE_OTEL_LOG_TAG, "otel task was unexpectedly closed");
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLOUT);
  __otel->should_exit = 1;
  vTaskDelete(NULL);
}
