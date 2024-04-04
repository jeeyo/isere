#include <string.h>
#include <unistd.h>

#include "isere.h"
#include "js.h"
#include "httpd.h"
#include "tcp.h"

int __http_handler(
  isere_t *isere,
  httpd_conn_t *conn,
  const char *method,
  const char *path,
  const char *query,
  httpd_header_t *request_headers,
  uint32_t request_headers_len,
  const char *body)
{
  isere_js_t js;
  memset(&js, 0, sizeof(isere_js_t));
  if (js_init(isere, &js) < 0) {
    // isere->logger->error(ISERE_LOG_TAG, "Unable to initialize JavaScript module");
    printf("Unable to initialize JavaScript module\n");
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
    // TODO: callbackWaitsForEmptyEventLoop
    JS_SetPropertyStr(js.context, context, "callbackWaitsForEmptyEventLoop", JS_NewBool(js.context, 1));
    JS_SetPropertyStr(js.context, global_obj, "__context", context);

    JS_FreeValue(js.context, global_obj);
  }

  // evaluate handler function
  // TODO: make this async
  js_eval(&js);

  // write response
  {
    JSValue global_obj = JS_GetGlobalObject(js.context);
    JSValue response_obj = JS_GetPropertyStr(js.context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);

    // send HTTP response code
    isere_tcp_write(conn->fd, "HTTP/1.1 ", 9);
    JSValue statusCode = JS_GetPropertyStr(js.context, response_obj, ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME);
    if (!JS_IsNumber(statusCode)) {
      isere_tcp_write(conn->fd, "200", 3);
    } else {
      size_t len = 0;
      isere_tcp_write(conn->fd, JS_ToCStringLen(js.context, &len, statusCode), len);
    }
    isere_tcp_write(conn->fd, "\r\n", 2);
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

          isere_tcp_write(conn->fd, header_field_str, strlen(header_field_str));
          isere_tcp_write(conn->fd, ": ", 2);
          isere_tcp_write(conn->fd, header_value_str, header_value_len);
          isere_tcp_write(conn->fd, "\r\n", 2);

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
    isere_tcp_write(conn->fd, server_header, strlen(server_header));
    isere_tcp_write(conn->fd, "\r\n", 2);

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
      isere_tcp_write(conn->fd, body_str, body_len);
      JS_FreeCString(js.context, body_str);
    }
    JS_FreeValue(js.context, body);

    JS_FreeValue(js.context, response_obj);
    JS_FreeValue(js.context, global_obj);

    isere_tcp_write(conn->fd, "\r\n\r\n", 4);
  }

  js_deinit(&js);
  return 0;
}
