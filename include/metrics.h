#ifndef ISERE_METRICS_H_
#define ISERE_METRICS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "mem.h"

// Startup phases of a JS request (criterion 10). With a warm engine the first
// four happen once at boot; with a cold engine they happen on every request.
typedef enum {
  ISERE_PHASE_RUNTIME = 0,  // engine runtime (QuickJS JSRuntime, JerryScript jerry_init)
  ISERE_PHASE_CONTEXT,      // context and intrinsics
  ISERE_PHASE_POLYFILLS,    // natives and src/prelude.js
  ISERE_PHASE_MODULE,       // compiling and evaluating the handler module
  ISERE_PHASE_CALL,         // the handler call and every job or timer it caused
  ISERE_PHASE_TEARDOWN,     // freeing what the request left behind
  ISERE_PHASE_COUNT
} isere_phase_t;

extern const char *isere_phase_names[ISERE_PHASE_COUNT];

// what one request cost (criterion 5)
typedef struct {
  uint64_t start_us;
  uint64_t wall_us;
  uint64_t cpu_ns;          // only stretches where this request's code ran
  isere_mem_account_t mem;  // bytes allocated, peak heap, net change
  uint32_t phase_us[ISERE_PHASE_COUNT];
  uint32_t conn_bytes;      // what the connection held for this request: buffers and spans
} isere_request_metrics_t;

// Starts/ends a stretch of work done for one request: its CPU time and its
// allocations are counted until the stretch ends. Stretches don't nest.
void isere_metrics_stretch_begin(isere_request_metrics_t *m);
void isere_metrics_stretch_end(void);
isere_request_metrics_t *isere_metrics_current(void);

// requests that matched no route (404/405) or were rejected before routing (400/413/431)
#define ISERE_METRICS_ROUTE_NONE (-1)

void isere_metrics_init(uint32_t num_routes, const char *const *methods, const char *const *paths);
void isere_metrics_deinit(void);
int isere_metrics_enabled(void);

// records a finished request against a route (or ISERE_METRICS_ROUTE_NONE)
void isere_metrics_record(int route, int status, const isere_request_metrics_t *m);

// boot timing
void isere_metrics_mark_boot(uint64_t boot_us);
void isere_metrics_mark_phase_boot(isere_phase_t phase, uint32_t us);
void isere_metrics_mark_first_request(void);

// heap a route's warm module instance keeps between requests
void isere_metrics_route_resident(int route, size_t bytes);

// connection pool
typedef enum {
  ISERE_CONN_ACCEPTED = 0,
  ISERE_CONN_CLOSED,
  ISERE_CONN_REJECTED,      // pool full, answered 503
  ISERE_CONN_EVICTED,       // idle keep-alive connection closed to make room
  ISERE_CONN_TIMED_OUT,     // request head or idle timeout
  ISERE_CONN_EVENT_COUNT
} isere_conn_event_t;
void isere_metrics_conn(isere_conn_event_t event);

// JS engine health
void isere_metrics_js_leftover_objects(uint32_t count);
void isere_metrics_js_instance(int created);
// an instance abandoned after a fatal engine error and replaced
void isere_metrics_js_restart(void);
void isere_metrics_js_mode(const char *mode);

// device-wide samples and the serial NDJSON stream; call once a second
void isere_metrics_tick(void);

// Server-Timing header value for a request; returns its length
int isere_metrics_server_timing(const isere_request_metrics_t *m, char *buf, size_t len);

// the /__metrics document; returns a buffer from isere_malloc() and its length
char *isere_metrics_json(size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_METRICS_H_ */
