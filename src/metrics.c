#include "metrics.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "mem.h"
#include "engine.h"

const char *isere_phase_names[ISERE_PHASE_COUNT] = {
  "runtime", "context", "polyfills", "module", "call", "teardown",
};

// latency histogram: 4 buckets per power of two of microseconds, about ±9% resolution
#define HIST_BUCKETS 128

typedef struct {
  uint64_t count;
  uint64_t errors;          // 5xx
  uint64_t sum_wall_us;
  uint64_t sum_cpu_ns;
  uint64_t sum_alloc;
  int64_t sum_net;
  uint64_t max_peak;
  uint64_t sum_conn_bytes;
  uint32_t max_conn_bytes;
  uint64_t sum_phase_us[ISERE_PHASE_COUNT];
  size_t resident;
  uint32_t hist[HIST_BUCKETS];
} route_stats_t;

static struct {
  int enabled;
  int ndjson;
  uint32_t num_routes;
  const char *const *methods;
  const char *const *paths;
  route_stats_t *routes;    // num_routes + 1; the last one is ISERE_METRICS_ROUTE_NONE
  const char *js_mode;

  uint64_t boot_us;
  uint64_t first_request_us;
  uint32_t boot_phase_us[ISERE_PHASE_COUNT];

  uint64_t conn_events[ISERE_CONN_EVENT_COUNT];
  uint64_t js_instances_created;
  uint64_t js_instances_freed;
  uint64_t js_leftover_objects;
  uint64_t js_leftover_teardowns;
  uint64_t js_restarts;

  // device CPU utilisation, sampled by isere_metrics_tick()
  uint64_t last_tick_us;
  uint64_t last_busy_ns;
  double cpu_utilisation;

  // the running stretch
  isere_request_metrics_t *current;
  uint64_t stretch_cpu_start;
  isere_mem_account_t *saved_account;
} __m;

static int __env_flag(const char *name, int fallback)
{
  const char *v = getenv(name);
  if (v == NULL || v[0] == '\0') {
    return fallback;
  }
  return v[0] != '0';
}

void isere_metrics_init(uint32_t num_routes, const char *const *methods, const char *const *paths)
{
  memset(&__m, 0, sizeof(__m));
  // ISERE_METRICS=0 turns accounting off, to measure what it costs (criterion 6)
  __m.enabled = __env_flag("ISERE_METRICS", 1);
  __m.ndjson = __m.enabled && __env_flag("ISERE_NDJSON", 1);
  __m.num_routes = num_routes;
  __m.methods = methods;
  __m.paths = paths;
  __m.routes = isere_calloc(num_routes + 1, sizeof(route_stats_t));
  __m.js_mode = "";
  __m.last_tick_us = platform_now_us();
  __m.last_busy_ns = platform_busy_cpu_ns();
}

void isere_metrics_deinit(void)
{
  isere_free(__m.routes);
  __m.routes = NULL;
}

int isere_metrics_enabled(void)
{
  return __m.enabled;
}

void isere_metrics_stretch_begin(isere_request_metrics_t *m)
{
  if (__m.current != NULL) {
    isere_metrics_stretch_end();
  }
  if (m == NULL || !__m.enabled) {
    return;
  }
  __m.current = m;
  __m.saved_account = isere_mem_get_account();
  isere_mem_set_account(&m->mem);
  __m.stretch_cpu_start = platform_task_cpu_ns();
}

void isere_metrics_stretch_end(void)
{
  if (__m.current == NULL) {
    return;
  }
  __m.current->cpu_ns += platform_task_cpu_ns() - __m.stretch_cpu_start;
  isere_mem_set_account(__m.saved_account);
  __m.saved_account = NULL;
  __m.current = NULL;
}

isere_request_metrics_t *isere_metrics_current(void)
{
  return __m.current;
}

static int __bucket(uint64_t us)
{
  if (us < 4) {
    return (int)us;
  }
  int msb = 63 - __builtin_clzll(us);
  int b = msb * 4 + (int)((us >> (msb - 2)) & 3);
  return b < HIST_BUCKETS ? b : HIST_BUCKETS - 1;
}

static uint64_t __bucket_value(int b)
{
  if (b < 4) {
    return (uint64_t)b;
  }
  int msb = b / 4;
  uint64_t lo = (uint64_t)(4 + b % 4) << (msb - 2);
  uint64_t width = 1ull << (msb - 2);
  return lo + width / 2;
}

static uint64_t __percentile(const route_stats_t *r, double q)
{
  if (r->count == 0) {
    return 0;
  }
  uint64_t rank = (uint64_t)(q * (double)r->count + 0.5);
  if (rank < 1) rank = 1;
  uint64_t seen = 0;
  for (int b = 0; b < HIST_BUCKETS; b++) {
    seen += r->hist[b];
    if (seen >= rank) {
      return __bucket_value(b);
    }
  }
  return __bucket_value(HIST_BUCKETS - 1);
}

static route_stats_t *__route(int route)
{
  if (__m.routes == NULL) {
    return NULL;
  }
  if (route < 0 || (uint32_t)route >= __m.num_routes) {
    return &__m.routes[__m.num_routes];
  }
  return &__m.routes[route];
}

void isere_metrics_record(int route, int status, const isere_request_metrics_t *m)
{
  if (!__m.enabled) {
    return;
  }
  route_stats_t *r = __route(route);
  if (r == NULL) {
    return;
  }

  r->count++;
  if (status >= 500) r->errors++;
  r->sum_wall_us += m->wall_us;
  r->sum_cpu_ns += m->cpu_ns;
  r->sum_alloc += m->mem.allocated;
  r->sum_net += m->mem.net;
  if (m->mem.peak_in_use > r->max_peak) r->max_peak = m->mem.peak_in_use;
  r->sum_conn_bytes += m->conn_bytes;
  if (m->conn_bytes > r->max_conn_bytes) r->max_conn_bytes = m->conn_bytes;
  for (int i = 0; i < ISERE_PHASE_COUNT; i++) {
    r->sum_phase_us[i] += m->phase_us[i];
  }
  r->hist[__bucket(m->wall_us)]++;

  if (__m.ndjson) {
    const char *path = (route >= 0 && (uint32_t)route < __m.num_routes) ? __m.paths[route] : "(unrouted)";
    printf("{\"t\":\"req\",\"path\":\"%s\",\"status\":%d,\"wall_us\":%" PRIu64 ",\"cpu_us\":%" PRIu64
      ",\"alloc\":%" PRIu64 ",\"peak\":%zu,\"net\":%" PRId64 ",\"conn_bytes\":%" PRIu32
      ",\"call_us\":%" PRIu32 "}\n",
      path, status, m->wall_us, m->cpu_ns / 1000, m->mem.allocated, m->mem.peak_in_use, m->mem.net,
      m->conn_bytes, m->phase_us[ISERE_PHASE_CALL]);
    fflush(stdout);
  }
}

void isere_metrics_mark_boot(uint64_t boot_us)
{
  __m.boot_us = boot_us;
}

void isere_metrics_mark_phase_boot(isere_phase_t phase, uint32_t us)
{
  __m.boot_phase_us[phase] += us;
}

void isere_metrics_mark_first_request(void)
{
  if (__m.first_request_us == 0) {
    __m.first_request_us = platform_now_us();
  }
}

void isere_metrics_route_resident(int route, size_t bytes)
{
  route_stats_t *r = __route(route);
  if (r != NULL) {
    r->resident = bytes;
  }
}

void isere_metrics_conn(isere_conn_event_t event)
{
  __m.conn_events[event]++;
}

void isere_metrics_js_leftover_objects(uint32_t count)
{
  __m.js_leftover_objects += count;
  if (count > 0) {
    __m.js_leftover_teardowns++;
  }
}

void isere_metrics_js_instance(int created)
{
  if (created) {
    __m.js_instances_created++;
  } else {
    __m.js_instances_freed++;
  }
}

void isere_metrics_js_restart(void)
{
  __m.js_restarts++;
}

void isere_metrics_js_mode(const char *mode)
{
  __m.js_mode = mode;
}

void isere_metrics_tick(void)
{
  uint64_t now = platform_now_us();
  uint64_t busy = platform_busy_cpu_ns();
  uint64_t wall_ns = (now - __m.last_tick_us) * 1000;
  if (wall_ns > 0) {
    __m.cpu_utilisation = (double)(busy - __m.last_busy_ns) / (double)wall_ns;
  }
  __m.last_tick_us = now;
  __m.last_busy_ns = busy;

  if (__m.ndjson) {
    isere_mem_stats_t mem;
    isere_mem_get_stats(&mem);
    printf("{\"t\":\"dev\",\"uptime_ms\":%" PRIu64 ",\"cpu\":%.4f,\"heap_free\":%zu,\"heap_min_free\":%zu"
      ",\"heap_in_use\":%zu}\n",
      (now - __m.boot_us) / 1000, __m.cpu_utilisation, isere_mem_free(), isere_mem_min_free(), mem.in_use);
    fflush(stdout);
  }
}

int isere_metrics_server_timing(const isere_request_metrics_t *m, char *buf, size_t len)
{
  // durations in milliseconds as the header defines them; byte counts go in desc
  int n = snprintf(buf, len,
    "wall;dur=%.3f, cpu;dur=%.3f, alloc;desc=\"%" PRIu64 "\", peak;desc=\"%zu\", net;desc=\"%" PRId64 "\", conn;desc=\"%" PRIu32 "\"",
    (double)m->wall_us / 1000.0, (double)m->cpu_ns / 1e6, m->mem.allocated, m->mem.peak_in_use, m->mem.net, m->conn_bytes);
  if (n < 0 || (size_t)n >= len) {
    return 0;
  }
  return n;
}

typedef struct {
  char *data;
  size_t len;
  size_t cap;
  int failed;
} json_buf_t;

static void __append(json_buf_t *b, const char *fmt, ...)
{
  if (b->failed) {
    return;
  }
  for (;;) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(b->data + b->len, b->cap - b->len, fmt, args);
    va_end(args);
    if (n < 0) {
      b->failed = 1;
      return;
    }
    if ((size_t)n < b->cap - b->len) {
      b->len += (size_t)n;
      return;
    }
    size_t cap = b->cap * 2 + (size_t)n;
    char *data = isere_realloc(b->data, cap);
    if (data == NULL) {
      b->failed = 1;
      return;
    }
    b->data = data;
    b->cap = cap;
  }
}

static void __append_phases(json_buf_t *b, const uint64_t *sums, uint64_t count)
{
  __append(b, "{");
  for (int i = 0; i < ISERE_PHASE_COUNT; i++) {
    __append(b, "%s\"%s\":%" PRIu64, i ? "," : "", isere_phase_names[i], count ? sums[i] / count : 0);
  }
  __append(b, "}");
}

char *isere_metrics_json(size_t *len)
{
  json_buf_t b = { isere_malloc(2048), 0, 2048, 0 };
  if (b.data == NULL) {
    return NULL;
  }

  isere_mem_stats_t mem;
  isere_mem_get_stats(&mem);
  uint64_t now = platform_now_us();

  __append(&b, "{\"build\":\"%s\",\"runtime\":\"%s\",\"js_mode\":\"%s\",\"metrics_enabled\":%s,",
    ISERE_BUILD_ID, ISERE_RUNTIME_NAME, __m.js_mode, __m.enabled ? "true" : "false");
  __append(&b, "\"device\":{\"uptime_ms\":%" PRIu64 ",\"cpu_utilisation\":%.4f,\"heap_size\":%zu,"
    "\"heap_free\":%zu,\"heap_min_free\":%zu,\"heap_in_use\":%zu,\"heap_peak\":%zu,\"allocations\":%" PRIu64 "},",
    (now - __m.boot_us) / 1000, __m.cpu_utilisation, mem.heap_size, isere_mem_free(), isere_mem_min_free(),
    mem.in_use, mem.peak_in_use, mem.allocations);

  __append(&b, "\"boot\":{\"to_first_request_ms\":%.3f,\"phases_us\":{",
    __m.first_request_us ? (double)(__m.first_request_us - __m.boot_us) / 1000.0 : 0.0);
  for (int i = 0; i < ISERE_PHASE_COUNT; i++) {
    __append(&b, "%s\"%s\":%" PRIu32, i ? "," : "", isere_phase_names[i], __m.boot_phase_us[i]);
  }
  __append(&b, "}},");

  __append(&b, "\"js\":{\"heap_used\":%zu,\"instances_created\":%" PRIu64 ",\"instances_freed\":%" PRIu64
    ",\"leftover_objects\":%" PRIu64 ",\"leftover_teardowns\":%" PRIu64 ",\"restarts\":%" PRIu64 "},",
    isere_engine_heap_used(), __m.js_instances_created, __m.js_instances_freed, __m.js_leftover_objects,
    __m.js_leftover_teardowns, __m.js_restarts);

  __append(&b, "\"connections\":{\"accepted\":%" PRIu64 ",\"closed\":%" PRIu64 ",\"rejected\":%" PRIu64
    ",\"evicted\":%" PRIu64 ",\"timed_out\":%" PRIu64 "},",
    __m.conn_events[ISERE_CONN_ACCEPTED], __m.conn_events[ISERE_CONN_CLOSED], __m.conn_events[ISERE_CONN_REJECTED],
    __m.conn_events[ISERE_CONN_EVICTED], __m.conn_events[ISERE_CONN_TIMED_OUT]);

  __append(&b, "\"routes\":[");
  for (uint32_t i = 0; i <= __m.num_routes; i++) {
    const route_stats_t *r = &__m.routes[i];
    uint64_t n = r->count;
    if (i < __m.num_routes) {
      __append(&b, "%s{\"method\":\"%s\",\"path\":\"%s\",", i ? "," : "", __m.methods[i], __m.paths[i]);
    } else {
      __append(&b, "%s{\"method\":\"\",\"path\":\"(unrouted)\",", i ? "," : "");
    }
    __append(&b, "\"count\":%" PRIu64 ",\"errors\":%" PRIu64 ",\"p50_us\":%" PRIu64 ",\"p99_us\":%" PRIu64
      ",\"mean_wall_us\":%" PRIu64 ",\"mean_cpu_us\":%" PRIu64 ",\"mean_alloc\":%" PRIu64 ",\"mean_net\":%" PRId64
      ",\"max_peak\":%" PRIu64 ",\"mean_conn_bytes\":%" PRIu64 ",\"max_conn_bytes\":%" PRIu32 ",\"resident\":%zu,\"mean_phase_us\":",
      n, r->errors, __percentile(r, 0.5), __percentile(r, 0.99),
      n ? r->sum_wall_us / n : 0, n ? r->sum_cpu_ns / n / 1000 : 0, n ? r->sum_alloc / n : 0,
      n ? r->sum_net / (int64_t)n : 0, r->max_peak, n ? r->sum_conn_bytes / n : 0, r->max_conn_bytes, r->resident);
    __append_phases(&b, r->sum_phase_us, n);
    __append(&b, "}");
  }
  __append(&b, "]}\n");

  if (b.failed) {
    isere_free(b.data);
    return NULL;
  }
  *len = b.len;
  return b.data;
}
