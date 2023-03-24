#ifndef ISERE_H_

#define ISERE_H_

#include <stdint.h>

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

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
  void (*error)(const char *fmt, ...);
  void (*warning)(const char *fmt, ...);
  void (*info)(const char *fmt, ...);
  void (*debug)(const char *fmt, ...);
} isere_logger_t;

typedef struct {
  void *dll;
  uint8_t *fn;
  uint32_t fn_size;
} isere_loader_t;

typedef struct {
  JSRuntime *runtime;
  JSContext *context;
} isere_js_t;

typedef struct {
  isere_logger_t *logger;
  isere_loader_t *loader;
} isere_t;

#ifdef __cplusplus
}
#endif

#endif /* ISERE_H_ */
