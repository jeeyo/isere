#include "logger.h"

#include <stdarg.h>

#include "esp_log.h"

static void logger_error(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  esp_log_writev(ESP_LOG_ERROR, tag, fmt, vargs);
  va_end(vargs);
}

static void logger_warning(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  esp_log_writev(ESP_LOG_WARN, tag, fmt, vargs);
  va_end(vargs);
}

static void logger_info(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  esp_log_writev(ESP_LOG_INFO, tag, fmt, vargs);
  va_end(vargs);
}

static void logger_debug(const char *tag, const char *fmt, ...)
{
  va_list vargs;
  va_start(vargs, fmt);
  esp_log_writev(ESP_LOG_DEBUG, tag, fmt, vargs);
  va_end(vargs);
}

int isere_logger_init(isere_logger_t *logger)
{
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
}