#include "isere.h"

#include "runtime.h"
#include "polyfills.h"

#include "jerryscript.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <string.h>
#include <stdint.h>

static SemaphoreHandle_t __ctx_mut = NULL;
static jerry_context_t *__current_context_p = NULL;

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

  const char *eval = "console.log('a');";
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
