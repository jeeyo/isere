#include "logger.h"

#include <stdarg.h>

// static SemaphoreHandle_t _stdio_mut = NULL;
// static StaticSemaphore_t _stdio_mutbuf;

static void __logger_print(const char *tag, isere_log_level_t level, const char *fmt, va_list vargs)
{
  // // xSemaphoreTake(_stdio_mut, portMAX_DELAY);

  // // TO-DO: log level
  // printf("[%s] ", tag);
  // vprintf(fmt, vargs);
  // printf("\n");

  // // xSemaphoreGive(_stdio_mut);
}

static void logger_error(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  __logger_print(tag, LOG_LEVEL_ERROR, fmt, vargs);
  va_end(vargs);
}

static void logger_warning(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  __logger_print(tag, LOG_LEVEL_WARNING, fmt, vargs);
  va_end(vargs);
}

static void logger_info(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  __logger_print(tag, LOG_LEVEL_INFO, fmt, vargs);
  va_end(vargs);
}

static void logger_debug(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  __logger_print(tag, LOG_LEVEL_DEBUG, fmt, vargs);
  va_end(vargs);
}

int isere_logger_init(isere_logger_t *logger)
{
  // if (_stdio_mut == NULL) {
  //   _stdio_mut = xSemaphoreCreateMutexStatic(&_stdio_mutbuf);
  // }

  *logger = (isere_logger_t){
    .error = &logger_error,
    .warning = &logger_warning,
    .info = &logger_info,
    .debug = &logger_debug,
  };

  return 0;
}

void isere_logger_deinit(isere_logger_t *logger)
{
  (void)logger;

  // if (_stdio_mut) {
  //   vSemaphoreDelete(_stdio_mut);
  //   _stdio_mut = NULL;
  // }
}