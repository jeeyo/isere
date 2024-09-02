#ifndef ISERE_H_
#define ISERE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "isere_config.h"

#include <stdint.h>

#include "platform.h"

#include "FreeRTOS.h"
#include "timers.h"

#include "quickjs.h"

#define ISERE_APP_NAME "isere"

#ifndef ISERE_APP_VERSION
#define ISERE_APP_VERSION "0.0.1"
#endif /* ISERE_APP_VERSION */

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
#ifdef ISERE_USE_DYNLINK
  void *dll;
#endif /* ISERE_USE_DYNLINK */
  uint8_t *fn;
  uint32_t fn_size;
} isere_loader_t;

#define ISERE_JS_POLYFILLS_MAX_TIMERS 5

typedef struct {
  TimerHandle_t timer;
  JSContext *ctx;
  JSValue func;
} polyfill_timer_t;

typedef struct {
  JSRuntime *runtime;
  JSContext *context;
  JSValue future;
  polyfill_timer_t timers[ISERE_JS_POLYFILLS_MAX_TIMERS];
} isere_js_context_t;

typedef void * isere_js_t;

typedef void * isere_httpd_t;

typedef void * isere_tcp_t;

typedef void * isere_fs_t;

typedef void * isere_ini_t;

typedef void * isere_rtc_t;

typedef struct {
  isere_logger_t *logger;
  isere_loader_t *loader;
  isere_js_t *js;
  isere_tcp_t *tcp;
  isere_httpd_t *httpd;
  isere_fs_t *fs;
  // isere_ini_t *ini;
  isere_rtc_t *rtc;
} isere_t;

#ifdef __cplusplus
}
#endif

#endif /* ISERE_H_ */
