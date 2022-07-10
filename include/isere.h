#ifndef ISERE_H_

#define ISERE_H_

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

typedef struct {
  void (*error)(const char *fmt, ...);
  void (*warning)(const char *fmt, ...);
  void (*info)(const char *fmt, ...);
  void (*debug)(const char *fmt, ...);
} isere_logger_t;

typedef struct {
  isere_logger_t *logger;
} isere_t;

#ifdef __cplusplus
}
#endif

#endif /* ISERE_H_ */
