#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __linux__
#include <signal.h>
#endif

#include "FreeRTOS.h"
#include "task.h"

#include "isere.h"

#include "logger.h"
#include "mem.h"
#include "metrics.h"
#include "otel.h"
#include "httpd.h"
#include "platform.h"
#include "tcp.h"

static isere_httpd_t httpd;

#ifdef ISERE_WITH_OTEL
static isere_otel_t *__otel = NULL;
#endif /* ISERE_WITH_OTEL */

#ifdef __linux__
static void on_signal(int signum)
{
  // only async-signal-safe work here: the httpd task notices the flag and shuts down
  (void)signum;
  httpd.should_exit = 1;
#ifdef ISERE_WITH_OTEL
  if (__otel != NULL) {
    __otel->should_exit = 1;
  }
#endif /* ISERE_WITH_OTEL */
}
#endif

int main(void)
{
  platform_init();
  uint64_t boot_us = platform_now_us();
  isere_mem_init(platform_heap_size());

  isere_logger_t logger;
  memset(&logger, 0, sizeof(isere_logger_t));
  if (isere_logger_init(&logger) < 0) {
    fprintf(stderr, "Unable to initialize logger module");
    return EXIT_FAILURE;
  }

  isere_rtc_t rtc;
  memset(&rtc, 0, sizeof(isere_rtc_t));
  if (isere_rtc_init(&rtc) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize rtc module");
    return EXIT_FAILURE;
  }

  isere_tcp_t tcp;
  memset(&tcp, 0, sizeof(isere_tcp_t));
  if (isere_tcp_init(&tcp) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize tcp module");
    return EXIT_FAILURE;
  }

#ifdef ISERE_WITH_OTEL
  isere_otel_t otel;
  memset(&otel, 0, sizeof(isere_otel_t));
  if (isere_otel_init(&otel, &logger, &rtc) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize otel module");
    return EXIT_FAILURE;
  }
  __otel = &otel;
#endif /* ISERE_WITH_OTEL */

  memset(&httpd, 0, sizeof(isere_httpd_t));
  if (isere_httpd_init(&httpd, &logger) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize httpd module");
    return EXIT_FAILURE;
  }
  isere_metrics_mark_boot(boot_us);

#ifdef __linux__
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
#endif /* __linux__ */

  // returns once the httpd task has shut down and ended the scheduler
  vTaskStartScheduler();

  isere_logger_deinit(&logger);
  return httpd.should_exit == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
}
