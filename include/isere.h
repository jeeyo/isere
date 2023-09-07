#ifndef ISERE_H_

#define ISERE_H_

#include <stdint.h>

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISERE_APP_NAME "isere"
#define ISERE_APP_VERSION "0.0.1"

#define ISERE_LOG_TAG "isere"

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
  int server_fd;
} isere_httpd_t;

typedef struct {
  void *k;
} isere_tcp_t;

typedef struct {
} isere_fs_t;

typedef struct {
} isere_ini_t;

typedef struct {
  void *rtc;
} isere_rtc_t;

typedef struct {
  isere_logger_t *logger;
  isere_loader_t *loader;
  isere_tcp_t *tcp;
  isere_httpd_t *httpd;
  isere_fs_t *fs;
  isere_rtc_t *rtc;
} isere_t;

#ifdef __cplusplus
}
#endif

#endif /* ISERE_H_ */
