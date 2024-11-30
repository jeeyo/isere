#ifndef ISERE_LOGGER_H_
#define ISERE_LOGGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "semphr.h"

typedef enum {
  LOG_LEVEL_ERROR = 40,
  LOG_LEVEL_WARNING = 30,
  LOG_LEVEL_INFO = 20,
  LOG_LEVEL_DEBUG = 10,
  LOG_LEVEL_NONE = 0,
} isere_log_level_t;

#define LOG_LEVEL_TO_STRING(level) \
  (level == LOG_LEVEL_ERROR ? "ERROR" : \
  (level == LOG_LEVEL_WARNING ? "WARNING" : \
  (level == LOG_LEVEL_INFO ? "INFO" : \
  (level == LOG_LEVEL_DEBUG ? "DEBUG" : \
  (level == LOG_LEVEL_NONE ? "NONE" : "UNKNOWN")))))

typedef struct {
  void (*error)(const char *tag, const char *fmt, ...);
  void (*warning)(const char *tag, const char *fmt, ...);
  void (*info)(const char *tag, const char *fmt, ...);
  void (*debug)(const char *tag, const char *fmt, ...);
} isere_logger_t;

int isere_logger_init(isere_logger_t *logger);
void isere_logger_deinit(isere_logger_t *logger);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_LOGGER_H_ */
