#include "isere.h"

#include "runtime.h"

#include <string.h>
#include <stdint.h>

static inline int32_t MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
static inline int32_t MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }

int js_runtime_process_response(isere_js_context_t *ctx, httpd_response_object_t *resp)
{
  JSValue global_obj = JS_GetGlobalObject(ctx->context);
  JSValue response_obj = JS_GetPropertyStr(ctx->context, global_obj, ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME);
  if (JS_IsUndefined(response_obj)) {
    resp->completed = 0;
    JS_FreeValue(ctx->context, response_obj);
    JS_FreeValue(ctx->context, global_obj);
    return -1;
  }

  resp->completed = 1;

  // HTTP response code
  JSValue statusCode = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_STATUS_CODE_PROP_NAME);
  if (!JS_IsNumber(statusCode)) {
    resp->statusCode = 200;
  } else {
    if (JS_ToUint32(ctx->context, &resp->statusCode, statusCode) < 0) {
      resp->statusCode = 500;
    }
  }
  JS_FreeValue(ctx->context, statusCode);

  // HTTP response headers
  JSValue headers = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_HEADERS_PROP_NAME);
  if (JS_IsObject(headers)) {

    JSPropertyEnum *props = NULL;
    uint32_t props_len = 0;

    if (JS_GetOwnPropertyNames(ctx->context, &props, &props_len, headers, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

      resp->num_header_fields = MIN(props_len, ISERE_HTTPD_MAX_HTTP_HEADERS);

      for (int i = 0; i < resp->num_header_fields; i++) {

        const char *header_field_str = JS_AtomToCString(ctx->context, props[i].atom);

        JSValue header_value = JS_GetProperty(ctx->context, headers, props[i].atom);
        size_t header_value_len = 0;
        const char *header_value_str = JS_ToCStringLen(ctx->context, &header_value_len, header_value);

        resp->header_names[i] = strdup(header_field_str);
        resp->header_values[i] = strdup(header_value_str);

        JS_FreeCString(ctx->context, header_field_str);
        JS_FreeCString(ctx->context, header_value_str);
        JS_FreeValue(ctx->context, header_value);

        JS_FreeAtom(ctx->context, props[i].atom);
      }

      js_free(ctx->context, props);
    }
  }
  JS_FreeValue(ctx->context, headers);

  // send HTTP response body
  JSValue body = JS_GetPropertyStr(ctx->context, response_obj, ISERE_JS_RESPONSE_BODY_PROP_NAME);
  size_t body_len = 0;
  const char *body_str = NULL;

  if (JS_IsObject(body)) {
    JSValue stringifiedBody = JS_JSONStringify(ctx->context, body, JS_UNDEFINED, JS_UNDEFINED);
    body_str = JS_ToCStringLen(ctx->context, &body_len, stringifiedBody);
    JS_FreeValue(ctx->context, stringifiedBody);
  } else if (JS_IsString(body)) {
    body_str = JS_ToCStringLen(ctx->context, &body_len, body);
  }

  if (body_str != NULL) {
    resp->body_len = body_len;
    resp->body = strdup(body_str);
    JS_FreeCString(ctx->context, body_str);
  }
  JS_FreeValue(ctx->context, body);

  JS_FreeValue(ctx->context, response_obj);
  JS_FreeValue(ctx->context, global_obj);

  return 0;
}
