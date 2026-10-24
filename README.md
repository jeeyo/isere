# isère

![workflow](https://github.com/jeeyo/isere/actions/workflows/ci.yml/badge.svg)

A small serverless runtime for microcontrollers: JavaScript endpoints on FreeRTOS, with QuickJS (quickjs-ng) or JerryScript, an HTTP server and a TLS client. It runs on the Raspberry Pi Pico 2 and, for development, as a Linux process.

The goal ([GOAL.md](GOAL.md)) is a small real-world service on a Pico 2: JWT verification against a cached JWKS, its own JWKS endpoint, and live per-request CPU and memory metrics, all with published benchmarks.

## What it does

- **Several endpoints**, each a JS module, routed by method and exact path ([`app/routes.json`](app/routes.json)).
  - Unknown routes answer 404 without touching an engine.
  - Static files are served without JS.
  - A failing, slow or runaway endpoint gets a 5xx and doesn't affect the others:
    - a throw gives 500
    - a busy loop or an endless `await` is interrupted at the handler deadline (504)
    - unbounded recursion gives 500
    - JerryScript running out of heap gives 500, and its instance is replaced
- **Warm engines.** Each module is evaluated once, and every request is a fresh call. On Linux, `ISERE_JS_MODE=cold` restores the old engine-per-request mode for comparison.
- **The Cloudflare Workers API shape**, the same for both engines, from one prelude ([`src/prelude.js`](src/prelude.js)):
  - `export default { fetch(request, env, ctx) }`
  - `Request`, `Response`, `Headers`, `URL`
  - `fetch` (HTTP and HTTPS)
  - `crypto.subtle` (`importKey('jwk')`, `verify` for RS256 and ES256, `digest`), `crypto.getRandomValues`
  - `TextEncoder`, `TextDecoder`, `atob`, `btoa`, timers
  - The older Lambda-style `export const handler` still works.
- **HTTP parsed in place** ([`src/http_parser.c`](src/http_parser.c)):
  - Each connection receives into its own buffer, and fields are `{offset, len}` spans into it.
  - The only limits are a head budget and a body budget (431 and 413).
  - Keep-alive, pipelining and chunked bodies are supported.
  - Ambiguous framing is rejected.
  - The parser is fuzzed with libFuzzer under ASan.
- **Per-request metrics**:
  - wall time, CPU time, bytes allocated, peak heap, net heap change and connection memory, in a `Server-Timing` header
  - per-endpoint p50/p99 and device CPU and heap at `/__metrics`
  - NDJSON on stdout or serial
  - calibration endpoints that show the figures are accurate within 10%
- **The reference service**, in [`app/`](app):
  - `GET /verify` checks `Authorization: Bearer <jwt>` against a JWKS cached in memory (TTL, one refetch for an unknown `kid`, single-flight). It works with the board's own `/.well-known/jwks.json` over loopback, or any JWKS over HTTPS.
  - `GET /.well-known/jwks.json` serves the board's own keys.

## Build and run (Linux)

```sh
git submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls
cmake -B build -DJS_RUNTIME=quickjs      # or jerryscript
cmake --build build -j
./build/isere                             # http://127.0.0.1:8080
```

On macOS, the FreeRTOS POSIX port and valgrind need Linux. Use the container that CI uses:

```sh
docker build -t isere-linux -f tools/linux.Dockerfile tools
docker run --rm -v "$PWD":/app -w /app isere-linux tools/linux.sh
```

| CMake option | Values | Default |
|---|---|---|
| `TARGET_PLATFORM` | `linux`, `pico2` | `linux` |
| `JS_RUNTIME` | `quickjs`, `jerryscript` | `quickjs` |
| `ISERE_APP_DIRS` | app directories to embed (`;`-separated) | `app` |
| `CMAKE_BUILD_TYPE` | | `RelWithDebInfo` |

| Environment (Linux) | Meaning |
|---|---|
| `ISERE_CONFIG` | JSON merged over the app's `config` (passed to handlers as `env`), e.g. `{"jwks_url":"https://…"}` |
| `ISERE_JS_MODE` | `warm` (default) or `cold` |
| `ISERE_JS_BYTECODE=1` | QuickJS: compile once and load instances from bytecode |
| `ISERE_JS_LAZY=0` | load all of the prelude up front |
| `ISERE_HANDLER_TIMEOUT_MS` | handler deadline (default 5000) |
| `ISERE_HEAD_BUDGET`, `ISERE_BODY_BUDGET` | per-connection budgets (default 8 KB and 16 KB) |
| `ISERE_MAX_CONNECTIONS` | connection pool (default 12; the Pico 2 has 4) |
| `ISERE_HEAP_SIZE` | heap budget (default 16 MB) |
| `ISERE_CA_FILE` | CA bundle for HTTPS `fetch` |
| `ISERE_METRICS=0`, `ISERE_NDJSON=0`, `ISERE_CPU_CLOCK=thread` | metrics switches |

## Validation and documents

- [tools/README.md](tools/README.md): the Linux and hardware validation loops, and the test suite
- [BENCHMARK.md](BENCHMARK.md): benchmarks with commit hashes
- [docs/FREERTOS_CONFIG.md](docs/FREERTOS_CONFIG.md): every FreeRTOS setting, classified and measured
- [docs/PLATFORM_DECISION.md](docs/PLATFORM_DECISION.md): FreeRTOS/C against the Zephyr/Rust spike
- [PORTING.md](PORTING.md)

## Acknowledgment

Special thanks to

- [maxnet](https://github.com/maxnet/pico-webserver/) for the TinyUSB RNDIS to lwIP glue for the Raspberry Pi Pico
- [libuv](https://github.com/libuv/libuv) for [src/internals/uv_poll.c](src/internals/uv_poll.c)
- [picohttpparser](https://github.com/h2o/picohttpparser), whose stateless design the HTTP parser follows
- [librdkafka](https://github.com/confluentinc/librdkafka) for OpenTelemetry nanopb encoding
