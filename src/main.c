#include <string.h>
#include <stdio.h>
#ifdef __linux__
#include <signal.h>
#endif

#include "FreeRTOS.h"
#include "task.h"

#include "isere.h"

// #include "fs.h"
// #include "ini.h"
#include "loader.h"
#include "logger.h"
#include "js.h"
#include "otel.h"
#include "httpd.h"
#include "tcp.h"

httpd_handler_t __http_handler;

static isere_logger_t *__logger = NULL;
static isere_httpd_t *__httpd = NULL;

#ifdef ISERE_WITH_OTEL
static isere_otel_t *__otel = NULL;
#endif /* ISERE_WITH_OTEL */

#ifdef __linux__
void sigint(int dummy) {
  __logger->info(ISERE_LOG_TAG, "Received SIGINT");

  __httpd->should_exit = 1;
#ifdef ISERE_WITH_OTEL
  __otel->should_exit = 1;
#endif /* ISERE_WITH_OTEL */

  // TODO
  // isere_httpd_deinit(isere.httpd);
  // isere_tcp_deinit(isere.tcp);
  // isere_js_deinit(isere.js);
  // isere_loader_deinit(isere.loader);
  // // fs_deinit(isere.fs);
  // // isere_ini_deinit(isere.ini);
  // isere_logger_deinit(isere.logger);

  // vTaskEndScheduler();
  exit(EXIT_SUCCESS);
}
#endif

int main(void)
{
  platform_init();

  // initialize logger module
  isere_logger_t logger;
  memset(&logger, 0, sizeof(isere_logger_t));
  if (isere_logger_init(&logger) < 0) {
    fprintf(stderr, "Unable to initialize logger module");
    return EXIT_FAILURE;
  }

  // initialize rtc module
  isere_rtc_t rtc;
  memset(&rtc, 0, sizeof(isere_rtc_t));
  if (isere_rtc_init(&rtc) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize rtc module");
    return EXIT_FAILURE;
  }

  // load handler.js
  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));
  if (isere_loader_init(&loader, &logger) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize loader module");
    return EXIT_FAILURE;
  }

  // initialize js module
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  if (isere_js_init(&js) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize js module");
    return EXIT_FAILURE;
  }

  // initialize tcp module
  isere_tcp_t tcp;
  memset(&tcp, 0, sizeof(isere_tcp_t));
  if (isere_tcp_init(&tcp) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize tcp module");
    return EXIT_FAILURE;
  }

#ifdef ISERE_WITH_OTEL
  // initialize otel module
  isere_otel_t otel;
  memset(&otel, 0, sizeof(isere_otel_t));
  if (isere_otel_init(&otel, &logger, &rtc) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize otel module");
    return EXIT_FAILURE;
  }
#endif /* ISERE_WITH_OTEL */

  // initialize web server module
  isere_httpd_t httpd;
  memset(&httpd, 0, sizeof(isere_httpd_t));
  if (isere_httpd_init(&httpd, &loader, &logger, &js, &__http_handler) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize httpd module");
    return EXIT_FAILURE;
  }

  __logger = &logger;
  __httpd = &httpd;
#ifdef ISERE_WITH_OTEL
  __otel = &otel;
#endif /* ISERE_WITH_OTEL */

#ifdef __linux__
  signal(SIGINT, sigint);
#endif /* __linux__ */

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  return EXIT_SUCCESS;
}
