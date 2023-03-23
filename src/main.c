/* Standard includes. */
#include <stdlib.h>

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Local includes. */
#include "isere.h"
#include "loader.h"
#include "logger.h"
#include "httpd.h"

static int _http_handler(const char *path, httpd_header_t *request_headers, uint32_t request_headers_len)
{
  printf("path: %s\n", path);
  return 0;
}

int main(void)
{
  // initialize logger module
  isere_logger_t logger;
  logger_init();
  logger_get_instance(&logger);

  isere_t isere;
  isere.logger = &logger;

  // dynamically loading javascript serverless handler
  loader_init(&isere);
  void *dl = loader_open(ISERE_LOADER_HANDLER_FILEPATH);
  if (!dl) {
    logger.error("Unable to open dynamic module");
  }

  uint32_t fn_size = 0;
  uint8_t *fn = loader_get_fn(dl, &fn_size);
  if (!fn) {
    logger.error("Unable to get handler function from dynamic module");
    loader_close(dl);
    return EXIT_FAILURE;
  }

  // js_ctx_t *js = js_init(&__isere);
  // js_eval(js, fn, fn_size);
  // js_deinit(js);

  httpd_init(&isere);

  TaskHandle_t httpd_task_handle;
  if (xTaskCreate(httpd_task, "httpd", configMINIMAL_STACK_SIZE, (void *)&_http_handler, tskIDLE_PRIORITY + 1, &httpd_task_handle) != pdPASS) {
    logger.error("Unable to create httpd task");
    goto cleanup;
  }

  vTaskStartScheduler();

  for(;;);

cleanup:
  loader_close(dl);
  return 0;
}
