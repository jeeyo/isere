/* Standard includes. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

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

static int __http_handler(isere_t *isere, isere_httpd_connection_t *conn, const char *method, const char *path, httpd_header_t *request_headers, uint32_t request_headers_len, const char *body)
{
  // TODO: parse url path and check if it matches a route

  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  if (js_init(isere, &js) < 0) {
    isere->logger->error(ISERE_LOG_TAG, "Unable to initialize JavaScript module");
    return -1;
  }

  // populate params
  {
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

    // TODO: check `Content-Type: application/json`
    JS_SetPropertyStr(js.context, global_obj, "__body", JS_NewString(js.context, body));
    const char *stringify = "try { JSON.parse(__body); } catch(e) { __body }";
    JSValue stringifiedBody = JS_Eval(js.context, stringify, strlen(stringify), "stringifyBody", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(js.context, event, "body", stringifiedBody);
    JS_FreeValue(js.context, stringifiedBody);

    // TODO: binary body
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
  }

  // evaluate handler function
  js_eval(&js);

  // get response object
  {
    JSValue global_obj = JS_GetGlobalObject(js.context);
    JSValue response_obj = JS_GetPropertyStr(js.context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);

    // send HTTP response code
    write(conn->fd, "HTTP/1.1 ", 9);
    JSValue statusCode = JS_GetPropertyStr(js.context, response_obj, ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME);
    if (!JS_IsNumber(statusCode)) {
      write(conn->fd, "200", 3);
    } else {
      size_t len = 0;
      write(conn->fd, JS_ToCStringLen(js.context, &len, statusCode), len);
    }
    write(conn->fd, "\r\n", 2);
    JS_FreeValue(js.context, statusCode);

    // send HTTP response headers
    JSValue headers = JS_GetPropertyStr(js.context, response_obj, ISERE_JS_RESPONSE_HEADERS_PROP_NAME);
    if (JS_IsObject(headers)) {

      JSPropertyEnum *props = NULL;
      uint32_t props_len = 0;

      // TODO: segfault when requested with Postman
      if (JS_GetOwnPropertyNames(js.context, &props, &props_len, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

        for (int i = 0; i < props_len; i++) {

          const char *header_field_str = JS_AtomToCString(js.context, props[i].atom);

          JSValue header_value = JS_GetProperty(js.context, headers, props[i].atom);
          size_t header_value_len = 0;
          const char *header_value_str = JS_ToCStringLen(js.context, &header_value_len, header_value);

          write(conn->fd, header_field_str, strlen(header_field_str));
          write(conn->fd, ": ", 2);
          write(conn->fd, header_value_str, header_value_len);
          write(conn->fd, "\r\n", 2);

          JS_FreeCString(js.context, header_field_str);
          JS_FreeCString(js.context, header_value_str);
          JS_FreeValue(js.context, header_value);

          JS_FreeAtom(js.context, props[i].atom);
        }

        js_free(js.context, props);
      }
    }
    JS_FreeValue(js.context, headers);

    // TODO: `Date`
    const char *server_header = "Server: isere\r\n";
    write(conn->fd, server_header, strlen(server_header));

    // TODO: send HTTP response body

    JS_FreeValue(js.context, response_obj);
    JS_FreeValue(js.context, global_obj);

    const char *buf = "\r\n\r\nTest\r\n\r\n";
    write(conn->fd, buf, strlen(buf));
  }

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
  isere_httpd_t httpd;
  memset(&httpd, 0, sizeof(isere_httpd_t));
  if (httpd_init(&isere, &httpd) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize httpd module");
    goto cleanup2;
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
    goto cleanup3;
  }

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  // cleanup
cleanup3:
  httpd_deinit(&httpd);
cleanup2:
  loader_deinit(&loader);
cleanup1:
  logger_deinit(&logger);
exit:
  return EXIT_SUCCESS;
}
