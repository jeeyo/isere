#include <stdarg.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <semphr.h>

static SemaphoreHandle_t _stdio_mut = NULL;
static StaticSemaphore_t _stdio_mutbuf;

void logger_init(void)
{
  if (_stdio_mut == NULL) {
    _stdio_mut = xSemaphoreCreateMutexStatic(&_stdio_mutbuf);
  }
}

void logger_debug(const char *fmt, ...)
{
  va_list vargs;

  va_start(vargs, fmt);

  xSemaphoreTake(_stdio_mut, portMAX_DELAY);

  vprintf(fmt, vargs);

  xSemaphoreGive(_stdio_mut);

  va_end(vargs);
}
