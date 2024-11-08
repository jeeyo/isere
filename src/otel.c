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

static isere_t *__isere = NULL;
static isere_otel_t *__otel = NULL;

static char __tx_buf[ISERE_OTEL_TX_BUF_LEN] = {0};
static otel_metrics_counter_t *__counters_head = NULL;

static int __connect_to_otel();
static inline void __disconnect_from_otel();

static void __otel_task(void *param);

static void __on_connected(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  uv__io_stop(&__otel->loop, w, UV_POLLOUT);

  __isere->logger->info(ISERE_OTEL_LOG_TAG, "Connected to OpenTelemetry Collector");
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

static inline void __disconnect_from_otel()
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

static bool encode_key_values(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  opentelemetry_proto_common_v1_KeyValue kv = {};
  kv.key.funcs.encode = &encode_string;
  kv.key.arg = "service.name";

  kv.has_value = true;
  kv.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
  kv.value.value.string_value.funcs.encode = &encode_string;
  kv.value.value.string_value.arg = ISERE_APP_NAME;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  if (!pb_encode_submessage(stream, opentelemetry_proto_common_v1_KeyValue_fields, &kv))
    return false;

  return true;
}

static bool encode_key_values2(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  opentelemetry_proto_common_v1_KeyValue kv = {};
  kv.key.funcs.encode = &encode_string;
  kv.key.arg = "my.counter.attr";

  kv.has_value = true;
  kv.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
  kv.value.value.string_value.funcs.encode = &encode_string;
  kv.value.value.string_value.arg = "bro";

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  if (!pb_encode_submessage(stream, opentelemetry_proto_common_v1_KeyValue_fields, &kv))
    return false;

  return true;
}

// TODO: Update to handle multiple data points.
static bool encode_number_data_point(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  opentelemetry_proto_metrics_v1_NumberDataPoint data_point = {};
  data_point.which_value = opentelemetry_proto_metrics_v1_NumberDataPoint_as_int_tag;
  data_point.value.as_int = 50;
  data_point.time_unix_nano = xTaskGetTickCount();
  data_point.attributes.funcs.encode = &encode_key_values2;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_submessage(stream, opentelemetry_proto_metrics_v1_NumberDataPoint_fields, &data_point);
}

static bool encode_metric(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
  opentelemetry_proto_metrics_v1_Metric metric = {};
  metric.which_data = opentelemetry_proto_metrics_v1_Metric_sum_tag;
  metric.data.sum.data_points.funcs.encode = &encode_number_data_point;
  metric.data.sum.data_points.arg = NULL;
  metric.data.sum.is_monotonic = true;
  metric.data.sum.aggregation_temporality =
    opentelemetry_proto_metrics_v1_AggregationTemporality_AGGREGATION_TEMPORALITY_CUMULATIVE;

  if (!pb_encode_tag_for_field(stream, field))
    return false;

  if (!pb_encode_submessage(stream, opentelemetry_proto_metrics_v1_Metric_fields, &metric))
    return false;

  return true;
}

static bool encode_scope_metrics(pb_ostream_t *stream,
                                 const pb_field_t *field,
                                 void *const *arg) {
        opentelemetry_proto_metrics_v1_ScopeMetrics *scope_metrics =
            (opentelemetry_proto_metrics_v1_ScopeMetrics *)*arg;
        if (!pb_encode_tag_for_field(stream, field))
                return false;

        return pb_encode_submessage(
            stream, opentelemetry_proto_metrics_v1_ScopeMetrics_fields,
            scope_metrics);
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

  while (!__isere->should_exit)
  {
    // poll sockets to see if there's any new events
    uv__io_poll(&__otel->loop, 0);

    // retry when connecting takes too long
    if (__otel->last_connect_attempt != 0 &&
        xTaskGetTickCount() - __otel->last_connect_attempt > pdMS_TO_TICKS(ISERE_OTEL_CONNECT_TIMEOUT_MS))
    {
      __isere->logger->warning(ISERE_OTEL_LOG_TAG, "Connection to OpenTelemetry Collector timed out, retrying...");
      __disconnect_from_otel();
      __connect_to_otel();
      continue;
    }

    if (__otel->last_connect_attempt == 0 &&
        xTaskGetTickCount() - __otel->last_sent > pdMS_TO_TICKS(3000))
    {
      __otel->last_sent = xTaskGetTickCount();

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
      resource_metrics.resource.attributes.funcs.encode = &encode_key_values;

      // if (__counters_head != NULL) {
      resource_metrics.scope_metrics.funcs.encode = &encode_scope_metrics;
      resource_metrics.scope_metrics.arg = &scope_metrics;

      metrics_data.resource_metrics.funcs.encode = &encode_resource_metrics;
      metrics_data.resource_metrics.arg = &resource_metrics;
      // }

      if (!pb_encode_delimited(&stream, opentelemetry_proto_metrics_v1_MetricsData_fields, &metrics_data)) {
        __isere->logger->error(ISERE_OTEL_LOG_TAG, "Encoding failed: %s\n", PB_GET_ERROR(&stream));
        continue;
      }

      printf("buf (%lu): ", stream.bytes_written);
      for (int i = 0; i < stream.bytes_written; i++) {
        printf("0x%x ", __tx_buf[0]);
      }
      printf("\r\n");

      char chunklen[8] = {0};
      int chunklen_len = 0;

      char buf[ISERE_OTEL_TX_BUF_LEN];

      tx_len = snprintf(buf, ISERE_OTEL_TX_BUF_LEN,
        "POST /v1/metrics HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/x-protobuf\r\n"
        "Content-Length: %lu\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: %s/%s\r\n"
        "\r\n",
        ISERE_OTEL_HOST, ISERE_OTEL_PORT,
        stream.bytes_written,
        ISERE_APP_NAME, ISERE_APP_VERSION
      );
      ret = isere_tcp_write(__otel->fd, buf, tx_len);
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
    }
  }

exit:
  __isere->logger->error(ISERE_OTEL_LOG_TAG, "otel task was unexpectedly closed");
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLOUT);
  __isere->should_exit = 1;
  vTaskDelete(NULL);
}
