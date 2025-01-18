#include "js.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "jerryscript.h"

SemaphoreHandle_t __ctx_mut = NULL;
isere_js_context_t *__current_isere_js_context = NULL;
jerry_context_t *__current_context_p = NULL;
