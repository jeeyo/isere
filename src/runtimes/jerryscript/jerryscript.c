#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static SemaphoreHandle_t __ctx_mut = NULL;
static jerry_context_t *__current_context_p = NULL;

static jerry_value_t __console_log(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
static jerry_value_t __console_warn(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
static jerry_value_t __console_error(const jerry_call_info_t *call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);

size_t jerry_port_context_alloc(size_t context_size)
{
  // TODO
  // size_t total_size = context_size + JERRY_GLOBAL_HEAP_SIZE * 1024;
  size_t total_size = context_size + (512 * 1024);
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

static inline void __lock_context() { xSemaphoreTake(__ctx_mut, portMAX_DELAY); }
static inline void __unlock_context() { xSemaphoreGive(__ctx_mut); }

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
  __lock_context();
  __current_context_p = ctx->context;

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
  // TODO: callbackWaitsForEmptyEventLoop
  __add_key_value_to_object(context, jerry_string_sz("callbackWaitsForEmptyEventLoop"), jerry_boolean(true));
  __add_key_value_to_object(global_obj, jerry_string_sz("context"), context);

  jerry_value_free(global_obj);

  const char *eval =
    "console.log('context', context);"
    "console.warn('event', event);"
    "console.error('process', process);";
  jerry_value_t eval_ret = jerry_eval((jerry_char_t *)eval, strlen(eval), JERRY_PARSE_NO_OPTS);
  if (!jerry_value_is_exception(eval_ret)) {
    jerry_value_free(eval_ret);
    return -1;
  }

  jerry_value_free(eval_ret);

  __current_context_p = NULL;
  __unlock_context();
  return 0;
}

int js_runtime_init_context(isere_js_t *js, isere_js_context_t *ctx)
{
  __lock_context();

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

  __current_context_p = NULL;
  __unlock_context();
  return 0;
}

int js_runtime_deinit_context(isere_js_t *js, isere_js_context_t *ctx)
{
  __lock_context();
  __current_context_p = ctx->context;

  jerry_cleanup();
  ctx->context = NULL;

  __current_context_p = NULL;
  __unlock_context();
  return 0;
}

int js_runtime_poll(isere_js_context_t *ctx)
{
  return 0;
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
    size_t len;
    jerry_char_t str[MAX_LOG_LENGTH_PER_LINE] = {0};

    if (jerry_value_is_object(arguments[i])) {
      jerry_value_t stringified = jerry_json_stringify(arguments[i]);
      jerry_size_t copied_bytes = jerry_string_to_buffer(stringified, JERRY_ENCODING_UTF8, str, sizeof(str) - 1);
      jerry_value_free(stringified);
    } else if (jerry_value_is_string(arguments[i])) {
      jerry_size_t copied_bytes = jerry_string_to_buffer(arguments[i], JERRY_ENCODING_UTF8, str, sizeof(str) - 1);
    }

    if (!str) {
      puts("(null)");
      continue;
    }

    puts(str);
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
