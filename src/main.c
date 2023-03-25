/* Standard includes. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Local includes. */
#include "isere.h"
#include "loader.h"
#include "logger.h"
#include "js.h"
#include "httpd.h"

#define ISERE_LOG_TAG "isere"

static int __http_handler(isere_t *isere, const char *method, const char *path, httpd_header_t *request_headers, uint32_t request_headers_len)
{
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  if (js_init(isere, &js) < 0) {
    isere->logger->error(ISERE_LOG_TAG, "Unable to initialize javascript module");
    return -1;
  }

  // populate params
  JSValue global_obj = JS_GetGlobalObject(js.context);
  // TODO: add `event` object: https://aws-lambda-for-python-developers.readthedocs.io/en/latest/02_event_and_context/
  JSValue event = JS_NewObject(js.context);
  JS_SetPropertyStr(js.context, event, "httpMethod", JS_NewString(js.context, method));
  JS_SetPropertyStr(js.context, event, "path", JS_NewString(js.context, path));

  JSValue headers = JS_NewObject(js.context);
  for (int i = 0; i < request_headers_len; i++) {
    JS_SetPropertyStr(js.context, headers, request_headers[i].name, JS_NewString(js.context, request_headers[i].value));
  }
  JS_SetPropertyStr(js.context, event, "headers", headers);
  // TODO: multi-value headers
  // TODO: query string params
  JS_SetPropertyStr(js.context, event, "body", JS_NULL);
  JS_SetPropertyStr(js.context, event, "isBase64Encoded", JS_FALSE);
  JS_SetPropertyStr(js.context, global_obj, "event", event);

  // add `context` object
  JSValue context = JS_NewObject(js.context);
  // // TODO: a C function?
  // JS_SetPropertyStr(js.context, context, "getRemainingTimeInMillis", NULL);
  // TODO: make some of these dynamic and some correct
  JS_SetPropertyStr(js.context, context, "functionName", JS_NewString(js.context, "handler"));
  JS_SetPropertyStr(js.context, context, "functionVersion", JS_NewString(js.context, "1.0.0"));
  JS_SetPropertyStr(js.context, context, "memoryLimitInMB", JS_NewInt32(js.context, 128));
  JS_SetPropertyStr(js.context, context, "logGroupName", JS_NewString(js.context, "isere"));
  JS_SetPropertyStr(js.context, context, "logStreamName", JS_NewString(js.context, "isere"));
  // JS_SetPropertyStr(js.context, context, "callbackWaitsForEmptyEventLoop", JS_NewBool(js.context, 1));
  JS_SetPropertyStr(js.context, global_obj, "context", context);

  JS_FreeValue(js.context, global_obj);

  js_eval(&js);

  js_deinit(&js);
  return 0;
}

int main(void)
{
  // create isere instance
  isere_t isere;
  memset(&isere, 0, sizeof(isere_t));

  // initialize logger module
  isere_logger_t logger;
  memset(&logger, 0, sizeof(isere_logger_t));
  if (logger_init(&isere, &logger) < 0) {
    fprintf(stderr, "Unable to initialize logger module");
    goto exit;
  }
  isere.logger = &logger;

  // dynamically loading javascript serverless handler
  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));
  if (loader_init(&isere, &loader, ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize loader module");
    goto cleanup1;
  }
  isere.loader = &loader;

  // initialize web server module
  httpd_init(&isere);

  // start web server task
  TaskHandle_t httpd_task_handle;
  if (xTaskCreate(httpd_task, "httpd", configMINIMAL_STACK_SIZE, (void *)&__http_handler, tskIDLE_PRIORITY + 1, &httpd_task_handle) != pdPASS) {
    logger.error(ISERE_LOG_TAG, "Unable to create httpd task");
    goto cleanup2;
  }

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  // cleanup
cleanup2:
  loader_deinit(&loader);
cleanup1:
  logger_deinit(&logger);
exit:
  return EXIT_SUCCESS;
}
