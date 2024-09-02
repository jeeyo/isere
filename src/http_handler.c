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
  isere_js_context_t *ctx = &conn->js;

  // populate params
  {
    JSValue global_obj = JS_GetGlobalObject(ctx->context);

    // TODO: add `event` object: https://aws-lambda-for-python-developers.readthedocs.io/en/latest/02_event_and_context/
    JSValue event = JS_NewObject(ctx->context);
    JS_SetPropertyStr(ctx->context, event, "httpMethod", JS_NewString(ctx->context, method));
    JS_SetPropertyStr(ctx->context, event, "path", JS_NewString(ctx->context, path));

    // TODO: multi-value headers
    JSValue headers = JS_NewObject(ctx->context);
    for (int i = 0; i < request_headers_len; i++) {
      JS_SetPropertyStr(ctx->context, headers, request_headers[i].name, JS_NewString(ctx->context, request_headers[i].value));
    }
    JS_SetPropertyStr(ctx->context, event, "headers", headers);

    // TODO: query string params
    // TODO: multi-value query string params
    JS_SetPropertyStr(ctx->context, event, "query", JS_NewString(ctx->context, query != NULL ? query : ""));

    // TODO: check `Content-Type: application/json`
    JSValue parsedBody = JS_ParseJSON(ctx->context, body, strlen(body), "<input>");
    if (!JS_IsObject(parsedBody)) {
      JS_FreeValue(ctx->context, parsedBody);
      parsedBody = JS_NewString(ctx->context, body);
    }
    JS_SetPropertyStr(ctx->context, event, "body", parsedBody);

    // TODO: binary body
    JS_SetPropertyStr(ctx->context, event, "isBase64Encoded", JS_FALSE);
    JS_SetPropertyStr(ctx->context, global_obj, "__event", event);

    // add `context` object
    JSValue context = JS_NewObject(ctx->context);
    // // TODO: a C function?
    // JS_SetPropertyStr(ctx->context, context, "getRemainingTimeInMillis", NULL);
    // TODO: make some of these dynamic and some correct
    JS_SetPropertyStr(ctx->context, context, "functionName", JS_NewString(ctx->context, "handler"));
    JS_SetPropertyStr(ctx->context, context, "functionVersion", JS_NewString(ctx->context, ISERE_APP_VERSION));
    JS_SetPropertyStr(ctx->context, context, "memoryLimitInMB", JS_NewInt32(ctx->context, 128));
    JS_SetPropertyStr(ctx->context, context, "logGroupName", JS_NewString(ctx->context, ISERE_APP_NAME));
    JS_SetPropertyStr(ctx->context, context, "logStreamName", JS_NewString(ctx->context, ISERE_APP_NAME));
    // TODO: callbackWaitsForEmptyEventLoop
    JS_SetPropertyStr(ctx->context, context, "callbackWaitsForEmptyEventLoop", JS_NewBool(ctx->context, 1));
    JS_SetPropertyStr(ctx->context, global_obj, "__context", context);

    JS_FreeValue(ctx->context, global_obj);
  }

  // evaluate handler function
  // TODO: make this async
  if (isere_js_eval(ctx, isere->loader->fn, isere->loader->fn_size) < 0) {
    // TODO: this should be logged
  }

  return 0;
}
