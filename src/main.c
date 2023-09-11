#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "isere.h"
#include "fs.h"
#include "ini.h"
#include "loader.h"
#include "logger.h"
#include "js.h"
#include "httpd.h"
#include "tcp.h"

httpd_handler_t __http_handler;
static isere_t isere;

void sigint(int dummy) {
  isere.logger->info(ISERE_LOG_TAG, "Received SIGINT");

  httpd_deinit(isere.httpd);
  tcp_deinit(isere.tcp);
  loader_deinit(isere.loader);
  fs_deinit(isere.fs);
  ini_deinit(isere.ini);
  logger_deinit(isere.logger);

  vTaskEndScheduler();
}

int main(void)
{
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

  // initialize file system module
  isere_fs_t fs;
  memset(&fs, 0, sizeof(isere_fs_t));
  if (fs_init(&isere, &fs) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize file system module");
    return EXIT_FAILURE;
  }
  isere.fs = &fs;

  // initialize configuration file module
  isere_ini_t ini;
  memset(&ini, 0, sizeof(isere_ini_t));
  if (ini_init(&isere, &ini) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize configuration file module");
    return EXIT_FAILURE;
  }
  isere.ini = &ini;

  logger.info(ISERE_LOG_TAG, "========== Configurations ==========");
  logger.info(ISERE_LOG_TAG, "  timeout: %d", ini_get_timeout(&ini));
  logger.info(ISERE_LOG_TAG, "====================================");

  // dynamically loading javascript serverless handler
  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));
  if (loader_init(&isere, &loader, ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize loader module");
    return EXIT_FAILURE;
  }
  isere.loader = &loader;

  // initialize tcp module
  isere_tcp_t tcp;
  memset(&tcp, 0, sizeof(isere_tcp_t));
  if (tcp_init(&isere, &tcp) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize tcp module");
    return EXIT_FAILURE;
  }
  isere.tcp = &tcp;

  // initialize web server module
  isere_httpd_t httpd;
  memset(&httpd, 0, sizeof(isere_httpd_t));
  if (httpd_init(&isere, &httpd) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize httpd module");
    return EXIT_FAILURE;
  }
  isere.httpd = &httpd;

  httpd_task_params_t httpd_params;
  memset(&httpd_params, 0, sizeof(httpd_task_params_t));
  httpd_params.httpd = &httpd;
  httpd_params.handler = &__http_handler;

  // start web server task
  TaskHandle_t httpd_task_handle;
  if (xTaskCreate(httpd_task, "httpd", configMINIMAL_STACK_SIZE, (void *)&httpd_params, tskIDLE_PRIORITY + 1, &httpd_task_handle) != pdPASS) {
    logger.error(ISERE_LOG_TAG, "Unable to create httpd task");
    return EXIT_FAILURE;
  }

  signal(SIGINT, sigint);

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  return EXIT_SUCCESS;
}
