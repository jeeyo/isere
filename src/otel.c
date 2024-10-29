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

static int __connect_to_otel();

static void __otel_task(void *param);

static void __on_poll(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  if (events & UV_POLLERR) {
    __isere->logger->debug(ISERE_OTEL_LOG_TAG, "UV_POLLERR");

    if (__otel->fd != -1) {
      isere_tcp_close(__otel->fd);
    }

    // __connect_to_otel();
  }

  if (events & UV_POLLIN) {
    __isere->logger->debug(ISERE_OTEL_LOG_TAG, "UV_POLLIN");
  }

  if (events & UV_POLLOUT) {
    __isere->logger->debug(ISERE_OTEL_LOG_TAG, "UV_POLLOUT");
  }
}

static void __on_connected(struct uv_loop_s* loop, struct uv__io_s* w, unsigned int events)
{
  uv__io_stop(&__otel->loop, w, UV_POLLOUT);

  w->fd = __otel->fd;
  w->events = 0;
  w->cb = __on_poll;
  w->opaque = NULL;
  uv__queue_init(&w->watcher_queue);

  uv__io_start(&__otel->loop, w, UV_POLLIN | UV_POLLOUT | UV_POLLERR);
}

static int __connect_to_otel()
{
  if ((__otel->fd = isere_tcp_socket_new()) < 0) {
    return -1;
  }

  if (isere_tcp_socket_set_nonblock(__otel->fd) < 0) {
    return -1;
  }

  int ret = isere_tcp_connect(__otel->fd, ISERE_OTEL_HOST, ISERE_OTEL_PORT);
  if (ret < 0 && ret != -2) {
    __isere->logger->error(ISERE_OTEL_LOG_TAG, "Unable to connect to OpenTelemetry Collector at %s:%d (%s %d)", ISERE_OTEL_HOST, ISERE_OTEL_PORT, strerror(errno), errno);
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

static void __otel_task(void *param)
{
  while (!isere_tcp_is_initialized()) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  if (__connect_to_otel() < 0) {
    goto exit;
  }

  while (!__isere->should_exit)
  {
    // poll sockets to see if there's any new events
    uv__io_poll(&__otel->loop, 0);
  }

exit:
  __isere->logger->error(ISERE_OTEL_LOG_TAG, "otel task was unexpectedly closed");
  uv__io_stop(&__otel->loop, &__otel->w, UV_POLLIN | UV_POLLOUT | UV_POLLERR);
  __isere->should_exit = 1;
  vTaskDelete(NULL);
}
