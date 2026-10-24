#ifndef ISERE_APP_H_
#define ISERE_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// The application served by isère, embedded at build time from one or more app
// directories (see tools/bundle_app.cmake). Each directory has a routes.json
// and the JS modules it names.

typedef struct {
  const char *name;         // file name, also the import specifier ("./jwt.js" -> "jwt.js")
  const char *source;
  uint32_t source_len;
} isere_app_module_t;

typedef enum {
  ISERE_ROUTE_JS = 0,       // handled by a JS module's handler
  ISERE_ROUTE_STATIC,       // a file served as is, without touching a JS engine
} isere_route_kind_t;

typedef struct {
  const char *method;       // "GET", "POST", ... or "*" for any
  const char *path;         // exact path
  isere_route_kind_t kind;
  int module;               // ISERE_ROUTE_JS: index into modules
  const char *content_type; // ISERE_ROUTE_STATIC
  const char *data;         // ISERE_ROUTE_STATIC
  uint32_t data_len;
} isere_app_route_t;

typedef struct {
  const isere_app_module_t *modules;
  uint32_t num_modules;
  const isere_app_route_t *routes;
  uint32_t num_routes;
  const char *config;       // JSON object, merged from every app directory
  uint32_t config_len;
  const char *prelude;      // src/prelude.js, shared by both engines
  uint32_t prelude_len;
  const isere_app_module_t *prelude_parts;  // src/prelude/*.js, compiled on first use
  uint32_t num_prelude_parts;
} isere_app_t;

extern const isere_app_t isere_app;

#ifdef __cplusplus
}
#endif

#endif /* ISERE_APP_H_ */
