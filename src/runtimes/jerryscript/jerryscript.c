#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript.h"
#include "jerryscript-ext/print.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifndef MAX
static inline int32_t MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
#endif /* MAX */

#ifndef MIN
static inline int32_t MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }
#endif /* MIN */

extern SemaphoreHandle_t __ctx_mut;
extern isere_js_context_t *__current_isere_js_context;
extern jerry_context_t *__current_context_p;

static jerry_value_t __handler_cb(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
static jerry_value_t __console_log(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
static jerry_value_t __console_warn(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
static jerry_value_t __console_error(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);

size_t jerry_port_context_alloc(size_t context_size)
{
  size_t total_size = context_size + JERRY_GLOBAL_HEAP_SIZE * 1024;
  __current_context_p = pvPortMalloc(total_size);

  return total_size;
}

jerry_context_t *jerry_port_context_get(void)
{
  return __current_context_p;
}

void jerry_port_context_free(void)
{
  vPortFree(__current_context_p);
}

int js_runtime_init(isere_js_t *js)
{
  __ctx_mut = xSemaphoreCreateMutex();
  if (__ctx_mut == NULL) {
    return -1;
  }

  return 0;
}

int js_runtime_deinit(isere_js_t *js)
{
  if (__ctx_mut != NULL) {
    vSemaphoreDelete(__ctx_mut);
  }

  return 0;
}

static inline void __lock_context(isere_js_context_t *ctx) {
  xSemaphoreTake(__ctx_mut, portMAX_DELAY);
  __current_context_p = ctx->context;
  __current_isere_js_context = ctx;
}
static inline void __unlock_context() {
  __current_context_p = NULL;
  __current_isere_js_context = NULL;
  xSemaphoreGive(__ctx_mut);
}

// will jerry_value_free() `key` and `value`, but not `object`
static inline int __add_key_value_to_object(jerry_value_t object, jerry_value_t key, jerry_value_t value)
{
  jerry_value_t set_result = jerry_object_set(object, key, value);
  jerry_value_free(key);
  jerry_value_free(value);

  /* Check if there was no error when adding the property (in this case it should never happen) */
  if (jerry_value_is_exception(set_result)) {
    jerry_value_free(set_result);
    return -1;
  }

  jerry_value_free(set_result);
  return 0;
}

// will jerry_value_free() `key`, but not `object`
static inline jerry_value_t __get_value_from_object_by_key(jerry_value_t object, jerry_value_t key)
{
  jerry_value_t ret = jerry_object_get(object, key);
  jerry_value_free(key);
  return ret;
}

static jerry_value_t module_resolve_callback(const jerry_value_t specifier,
                                              const jerry_value_t referrer,
                                              void *user_data_p)
{
  // This resolves only in `isere` main script

  // TODO: pass `handler_size` as parameter
  const char *handler = (const char *)user_data_p;

  jerry_parse_options_t parse_options;
  parse_options.options = JERRY_PARSE_MODULE | JERRY_PARSE_HAS_SOURCE_NAME;
  parse_options.source_name = jerry_string_sz("handler");

  jerry_value_t result = jerry_parse(handler, strlen(handler), &parse_options);
  jerry_value_free(parse_options.source_name);
  return result;
}

int js_runtime_eval_handler(
  isere_js_context_t *ctx,
  unsigned char *handler,
  unsigned int handler_len,
  const char *method,
  const char *path,
  const char *query,
  const char **request_header_names,
  const char **request_header_values,
  const uint32_t request_headers_len,
  const char *body)
{
  __lock_context(ctx);

  jerry_value_t global_obj = jerry_current_realm();

  // TODO: `event` object: https://aws-lambda-for-python-developers.readthedocs.io/en/latest/02_event_and_context/
  jerry_value_t event = jerry_object();
  __add_key_value_to_object(event, jerry_string_sz("httpMethod"), jerry_string_sz(method));
  __add_key_value_to_object(event, jerry_string_sz("path"), jerry_string_sz(path));

  // TODO: multi-value headers
  jerry_value_t headers = jerry_object();
  {
    int i = request_headers_len;
    char *name = (char *)request_header_names;
    char *value = (char *)request_header_values;
    while (i) {
      __add_key_value_to_object(headers, jerry_string_sz(name), jerry_string_sz(value));
      if (--i == 0) break;

      while (*(++name) != '\0');
      while (*(++name) == '\0');
      while (*(++value) != '\0');
      while (*(++value) == '\0');
    }
  }
  __add_key_value_to_object(event, jerry_string_sz("headers"), headers);

  // TODO: query string params
  // TODO: multi-value query string params
  __add_key_value_to_object(event, jerry_string_sz("query"), jerry_string_sz(query != NULL ? query : ""));

  // TODO: check `Content-Type: application/json`
  jerry_value_t parsedBody = jerry_json_parse(body, strlen(body));
  if (!jerry_value_is_object(parsedBody)) {
    jerry_value_free(parsedBody);
    parsedBody = jerry_string_sz(body);
  }
  __add_key_value_to_object(event, jerry_string_sz("body"), parsedBody);

  // TODO: binary body
  __add_key_value_to_object(event, jerry_string_sz("isBase64Encoded"), jerry_boolean(false));
  __add_key_value_to_object(global_obj, jerry_string_sz("event"), event);

  // `context` object
  jerry_value_t context = jerry_object();
  // // TODO: a C function?
  // JS_SetPropertyStr(ctx->context, context, "getRemainingTimeInMillis", NULL);
  // TODO: make some of these dynamic and some correct
  __add_key_value_to_object(context, jerry_string_sz("functionName"), jerry_string_sz("handler"));
  __add_key_value_to_object(context, jerry_string_sz("functionVersion"), jerry_string_sz(ISERE_APP_VERSION));
  __add_key_value_to_object(context, jerry_string_sz("memoryLimitInMB"), jerry_number(128));
  __add_key_value_to_object(context, jerry_string_sz("logGroupName"), jerry_string_sz(ISERE_APP_NAME));
  __add_key_value_to_object(context, jerry_string_sz("logStreamName"), jerry_string_sz(ISERE_APP_NAME));
  __add_key_value_to_object(context, jerry_string_sz("callbackWaitsForEmptyEventLoop"), jerry_boolean(true));
  __add_key_value_to_object(global_obj, jerry_string_sz("context"), context);

  // callback for getting result
  __add_key_value_to_object(global_obj, jerry_string_sz("cb"), jerry_function_external(__handler_cb));

  jerry_value_free(global_obj);

  int ret = -1;

  jerry_parse_options_t parse_options;
  parse_options.options = JERRY_PARSE_MODULE | JERRY_PARSE_HAS_SOURCE_NAME;
  parse_options.source_name = jerry_string_sz("isere");

  const char *eval =
    "import { handler } from 'handler';"
    "const handler1 = new Promise(resolve => handler(event, context, resolve).then(resolve));"
    "Promise.resolve(handler1).then(cb)";

  jerry_value_t e = jerry_parse((const jerry_char_t *)eval, strlen(eval), &parse_options);
  if (jerry_value_is_exception(e)) {
    jerryx_print_unhandled_exception(e);
    goto fail;
  }
  jerry_value_free(e);

  jerry_value_t e_result = jerry_module_link(e, module_resolve_callback, handler);
  if (jerry_value_is_exception(e_result)) {
    jerryx_print_unhandled_exception(e_result);
    goto fail_result;
  }
  jerry_value_free(e_result);

  e_result = jerry_module_evaluate(e);
  if (jerry_value_is_exception(e_result)) {
    jerryx_print_unhandled_exception(e_result);
    goto fail_result;
  }

  ret = 0;

fail_result:
  jerry_value_free(e_result);
fail:
  jerry_value_free(e);
  jerry_value_free(parse_options.source_name);

  __unlock_context();
  return ret;
}

int js_runtime_init_context(isere_js_t *js, isere_js_context_t *ctx)
{
  __lock_context(ctx);

  jerry_init(JERRY_INIT_EMPTY);
  ctx->context = __current_context_p;

  jerry_value_t global_obj = jerry_current_realm();
  
  // add console.log(), console.warn(), and console.error() function
  jerry_value_t console = jerry_object();
  __add_key_value_to_object(console, jerry_string_sz("log"), jerry_function_external(__console_log));
  __add_key_value_to_object(console, jerry_string_sz("warn"), jerry_function_external(__console_warn));
  __add_key_value_to_object(console, jerry_string_sz("error"), jerry_function_external(__console_error));
  __add_key_value_to_object(global_obj, jerry_string_sz("console"), console);

  // add process.env
  jerry_value_t process = jerry_object();
  jerry_value_t env = jerry_object();
  __add_key_value_to_object(env, jerry_string_sz("NODE_ENV"), jerry_string_sz("production"));
  __add_key_value_to_object(process, jerry_string_sz("env"), env);
  __add_key_value_to_object(global_obj, jerry_string_sz("process"), process);

  jerry_value_free(global_obj);

  __unlock_context();
  return 0;
}

int js_runtime_deinit_context(isere_js_t *js, isere_js_context_t *ctx)
{
  __lock_context(ctx);

  jerry_cleanup();
  ctx->context = NULL;

  __unlock_context();
  return 0;
}

int js_runtime_poll(isere_js_context_t *ctx)
{
  __lock_context(ctx);

  int ret = 1;

  jerry_value_t job_value = jerry_run_jobs();
  if (jerry_value_is_exception(job_value)) {
    if (jerry_value_is_abort(job_value)) {
      ret = -1;
      goto finally;
    }

    jerryx_print_unhandled_exception(job_value);
    ret = -1;
    goto finally;
  } else {
    ret = 0;
    goto finally;
  }

finally:
  jerry_value_free(job_value);
  __unlock_context();
  return ret;
}

static jerry_value_t __handler_cb(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count)
{
  if (argument_count < 1) {
    return jerry_throw_sz(JERRY_ERROR_COMMON, "response is not an object");
  }

  isere_js_context_t *context = __current_isere_js_context;
  httpd_response_object_t *response = &((httpd_conn_t *)context->opaque)->response;

  jerry_value_t response_obj = arguments[0];

  // HTTP response code
  jerry_value_t statusCode = __get_value_from_object_by_key(response_obj, jerry_string_sz("statusCode"));
  if (!jerry_value_is_number(statusCode)) {
    response->statusCode = 200;
  } else {
    response->statusCode = jerry_value_as_int32(statusCode);
  }
  jerry_value_free(statusCode);

  // HTTP response headers
  jerry_value_t headers = __get_value_from_object_by_key(response_obj, jerry_string_sz("headers"));
  if (jerry_value_is_object(headers))
  {
    jerry_value_t props = jerry_object_keys(headers);
    jerry_length_t props_len = jerry_array_length(props);

    response->num_header_fields = MIN(props_len, ISERE_HTTPD_MAX_HTTP_HEADERS);

    for (int i = 0; i < response->num_header_fields; i++)
    {
      jerry_value_t header_name = jerry_object_get_index(props, i);
      jerry_value_t header_field = jerry_object_get(headers, header_name);

      if (jerry_value_is_string(header_name) &&
          jerry_value_is_string(header_field))
      {
        jerry_size_t header_name_len = jerry_string_size(header_name, JERRY_ENCODING_UTF8);
        response->header_names[i] = (char *)pvPortMalloc(header_name_len + 1);
        jerry_string_to_buffer(header_name, JERRY_ENCODING_UTF8, response->header_names[i], header_name_len);
        response->header_names[i][header_name_len] = '\0';

        jerry_size_t header_field_len = jerry_string_size(header_field, JERRY_ENCODING_UTF8);
        response->header_values[i] = (char *)pvPortMalloc(header_field_len + 1);
        jerry_string_to_buffer(header_field, JERRY_ENCODING_UTF8, response->header_values[i], header_field_len);
        response->header_values[i][header_field_len] = '\0';
      }

      jerry_value_free(header_name);
      jerry_value_free(header_field);
    }

    jerry_value_free(props);
  }
  jerry_value_free(headers);

  // send HTTP response body
  jerry_value_t body = __get_value_from_object_by_key(response_obj, jerry_string_sz("body"));
  size_t body_len = 0;
  char *body_str = NULL;

  if (jerry_value_is_object(body)) {
    jerry_value_t stringifiedBody = jerry_json_stringify(body);
    body_len = jerry_string_size(stringifiedBody, JERRY_ENCODING_UTF8);
    body_str = (char *)pvPortMalloc(body_len + 1);
    jerry_string_to_buffer(stringifiedBody, JERRY_ENCODING_UTF8, body_str, body_len);
    body_str[body_len] = '\0';
    jerry_value_free(stringifiedBody);
  } else if (jerry_value_is_string(body)) {
    body_len = jerry_string_size(body, JERRY_ENCODING_UTF8);
    body_str = (char *)pvPortMalloc(body_len + 1);
    jerry_string_to_buffer(body, JERRY_ENCODING_UTF8, body_str, body_len);
    body_str[body_len] = '\0';
  }

  if (body_str != NULL) {
    response->body_len = body_len;
    response->body = strdup(body_str);
    vPortFree(body_str);
  } else {
    // TODO: prepare memory in advance instead of strdup()/malloc() from runtime port
    response->body = pvPortMalloc(1);
  }

  jerry_value_free(body);

  response->completed = 1;
  return jerry_undefined();
}

#define MAX_LOG_LENGTH_PER_LINE 256

static jerry_value_t __logger_internal(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t arguments[],
                                      const jerry_length_t argument_count,
                                      const char *color)
{
  puts(color);

  for (int i = 0; i < argument_count; i++) {
    // add space between arguments
    if (i != 0) {
      puts(" ");
    }

    // convert argument to C string
    size_t len = 0;
    char *str = NULL;

    if (jerry_value_is_function(arguments[i])) {
      const char *fn = "[Function]";
      str = strdup(fn);
    } else if (jerry_value_is_object(arguments[i])) {
      jerry_value_t stringified = jerry_json_stringify(arguments[i]);
      len = jerry_string_size(stringified, JERRY_ENCODING_UTF8);
      str = (char *)pvPortMalloc(len + 1);
      jerry_string_to_buffer(stringified, JERRY_ENCODING_UTF8, str, len);
      str[len] = '\0';
      jerry_value_free(stringified);
    } else if (jerry_value_is_string(arguments[i])) {
      len = jerry_string_size(arguments[i], JERRY_ENCODING_UTF8);
      str = (char *)pvPortMalloc(len + 1);
      jerry_string_to_buffer(arguments[i], JERRY_ENCODING_UTF8, str, len);
      str[len] = '\0';
    }

    if (!str) {
      puts("(null)");
      continue;
    }

    puts(str);
    vPortFree(str);
  }

  puts("\033[0m\n");

  return jerry_undefined();
}

static jerry_value_t __console_log(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count)
{
  return __logger_internal(call_info_p, arguments, argument_count, "\033[0m");
}

static jerry_value_t __console_warn(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count)
{
  return __logger_internal(call_info_p, arguments, argument_count, "\033[0;33m");
}

static jerry_value_t __console_error(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count)
{
  return __logger_internal(call_info_p, arguments, argument_count, "\033[0;31m");
}
