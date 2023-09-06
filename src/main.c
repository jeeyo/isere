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

static int __http_handler(
  isere_t *isere,
  isere_httpd_conn_t *conn,
  const char *method,
  const char *path,
  const char *query,
  httpd_header_t *request_headers,
  uint32_t request_headers_len,
  const char *body)
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

    // TODO: multi-value headers
    JSValue headers = JS_NewObject(js.context);
    for (int i = 0; i < request_headers_len; i++) {
      JS_SetPropertyStr(js.context, headers, request_headers[i].name, JS_NewString(js.context, request_headers[i].value));
    }
    JS_SetPropertyStr(js.context, event, "headers", headers);

    // TODO: query string params
    // TODO: multi-value query string params
    JS_SetPropertyStr(js.context, event, "query", JS_NewString(js.context, query != NULL ? query : ""));

    // TODO: check `Content-Type: application/json`
    JSValue parsedBody = JS_ParseJSON(js.context, body, strlen(body), "<input>");
    if (!JS_IsObject(parsedBody)) {
      JS_FreeValue(js.context, parsedBody);
      parsedBody = JS_NewString(js.context, body);
    }
    JS_SetPropertyStr(js.context, event, "body", parsedBody);

    // TODO: binary body
    JS_SetPropertyStr(js.context, event, "isBase64Encoded", JS_FALSE);
    JS_SetPropertyStr(js.context, global_obj, "__event", event);

    // add `context` object
    JSValue context = JS_NewObject(js.context);
    // // TODO: a C function?
    // JS_SetPropertyStr(js.context, context, "getRemainingTimeInMillis", NULL);
    // TODO: make some of these dynamic and some correct
    JS_SetPropertyStr(js.context, context, "functionName", JS_NewString(js.context, "handler"));
    JS_SetPropertyStr(js.context, context, "functionVersion", JS_NewString(js.context, ISERE_APP_VERSION));
    JS_SetPropertyStr(js.context, context, "memoryLimitInMB", JS_NewInt32(js.context, 128));
    JS_SetPropertyStr(js.context, context, "logGroupName", JS_NewString(js.context, ISERE_APP_NAME));
    JS_SetPropertyStr(js.context, context, "logStreamName", JS_NewString(js.context, ISERE_APP_NAME));
    // JS_SetPropertyStr(js.context, context, "callbackWaitsForEmptyEventLoop", JS_NewBool(js.context, 1));
    JS_SetPropertyStr(js.context, global_obj, "__context", context);

    JS_FreeValue(js.context, global_obj);
  }

  // evaluate handler function
  js_eval(&js);

  // write response
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
    write(conn->fd, "\r\n", 2);

    // send HTTP response body
    JSValue body = JS_GetPropertyStr(js.context, response_obj, ISERE_JS_RESPONSE_BODY_PROP_NAME);
    size_t body_len = 0;
    const char *body_str = NULL;

    if (JS_IsObject(body)) {
      JSValue stringifiedBody = JS_JSONStringify(js.context, body, JS_UNDEFINED, JS_UNDEFINED);
      body_str = JS_ToCStringLen(js.context, &body_len, stringifiedBody);
      JS_FreeValue(js.context, stringifiedBody);
    } else if (JS_IsString(body)) {
      body_str = JS_ToCStringLen(js.context, &body_len, body);
    }

    if (body_str != NULL) {
      write(conn->fd, body_str, body_len);
      JS_FreeCString(js.context, body_str);
    }
    JS_FreeValue(js.context, body);

    JS_FreeValue(js.context, response_obj);
    JS_FreeValue(js.context, global_obj);

    write(conn->fd, "\r\n\r\n", 4);
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
    return EXIT_FAILURE;
  }
  isere.logger = &logger;

  // dynamically loading javascript serverless handler
  isere_loader_t loader;
  memset(&loader, 0, sizeof(isere_loader_t));
  if (loader_init(&isere, &loader, ISERE_LOADER_HANDLER_FUNCTION_DLL_PATH) < 0) {
    logger.error(ISERE_LOG_TAG, "Unable to initialize loader module");
    return EXIT_FAILURE;
  }
  isere.loader = &loader;

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

  // start FreeRTOS scheduler
  vTaskStartScheduler();

  return EXIT_SUCCESS;
}
