#include "isere.h"

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

static void _logger_print(isere_log_level_t level, const char *fmt, va_list vargs)
{
  xSemaphoreTake(_stdio_mut, portMAX_DELAY);

  // TO-DO: log level
  vprintf(fmt, vargs);

  xSemaphoreGive(_stdio_mut);
}

static void logger_error(const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  _logger_print(LOG_LEVEL_ERROR, fmt, vargs);
  va_end(vargs);
}

static void logger_warning(const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  _logger_print(LOG_LEVEL_WARNING, fmt, vargs);
  va_end(vargs);
}

static void logger_info(const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  _logger_print(LOG_LEVEL_INFO, fmt, vargs);
  va_end(vargs);
}

static void logger_debug(const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  _logger_print(LOG_LEVEL_DEBUG, fmt, vargs);
  va_end(vargs);
}

void logger_get_instance(isere_logger_t *logger)
{
  *logger = (isere_logger_t){
    .error = &logger_error,
    .warning = &logger_warning,
    .info = &logger_info,
    .debug = &logger_debug,
  };
}
