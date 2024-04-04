#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "isere.h"
// #include "fs.h"
// #include "ini.h"
#include "loader.h"
#include "logger.h"
#include "js.h"
#include "httpd.h"
#include "tcp.h"

#include "platform.h"

httpd_handler_t __http_handler;
static isere_t isere;

#ifdef __linux__
void sigint(int dummy) {
  isere.logger->info(ISERE_LOG_TAG, "Received SIGINT");

  isere_httpd_deinit(isere.httpd);
  isere_tcp_deinit(isere.tcp);
  loader_deinit(isere.loader);
  // fs_deinit(isere.fs);
  // ini_deinit(isere.ini);
  logger_deinit(isere.logger);

  vTaskEndScheduler();
}
#endif

int main(void)
{
  platform_init();

  // create isere instance
  memset(&isere, 0, sizeof(isere_t));

  // initialize logger module
  isere_logger_t logger;
  memset(&logger, 0, sizeof(isere_logger_t));
  if (logger_init(&isere, &logger) < 0) {
    fprintf(stderr, "Unable to initialize logger module");
    return EXIT_FAILURE;
  }
  isere.logger = &logger;
  
  // // initialize file system module
  // isere_fs_t fs;
  // memset(&fs, 0, sizeof(isere_fs_t));
  // if (fs_init(&isere, &fs) < 0) {
  //   logger.error(ISERE_LOG_TAG, "Unable to initialize file system module");
  //   return EXIT_FAILURE;
  // }
  // isere.fs = &fs;

  // // initialize configuration file module
  // isere_ini_t ini;
  // memset(&ini, 0, sizeof(isere_ini_t));
  // if (ini_init(&isere, &ini) < 0) {
  //   logger.error(ISERE_LOG_TAG, "Unable to initialize configuration file module");
  //   return EXIT_FAILURE;
  // }
  // isere.ini = &ini;

  // logger.info(ISERE_LOG_TAG, "========== Configurations ==========");
  // logger.info(ISERE_LOG_TAG, "  timeout: %d", ini_get_timeout(&ini));
  // logger.info(ISERE_LOG_TAG, "====================================");

  // dynamically loading javascript serverless handler
  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));
  if (loader_init(&isere, &loader) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize loader module");
    return EXIT_FAILURE;
  }
  isere.loader = &loader;

  // initialize tcp module
  isere_tcp_t tcp;
  memset(&tcp, 0, sizeof(isere_tcp_t));
  if (isere_tcp_init(&isere, &tcp) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize tcp module");
    return EXIT_FAILURE;
  }
  isere.tcp = &tcp;

  // initialize web server module
  isere_httpd_t httpd;
  memset(&httpd, 0, sizeof(isere_httpd_t));
  if (isere_httpd_init(&isere, &httpd, &__http_handler) < 0) {
  // if (isere_httpd_init(&isere, &httpd, NULL) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize httpd module");
    return EXIT_FAILURE;
  }
  isere.httpd = &httpd;

#ifdef __linux__
  signal(SIGINT, sigint);
#endif

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  return EXIT_SUCCESS;
}
