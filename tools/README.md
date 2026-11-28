# Validation loops

Two scripts run the same black-box test suite, `tests/e2e/*.test.mjs`. It's written with Node's built-in [`node:test`](https://nodejs.org/api/test.html) runner and has no dependencies.

| Script | Target | Use it |
|---|---|---|
| `tools/linux.sh` | Linux build on this machine | first, on every change: about three minutes per iteration, including fuzzing and valgrind |
| `tools/hil.sh` | a real board: the NXP FRDM-K64F through its OpenSDA port | once the Linux loop is green |

Both write `hil-results/<build-id>/`, containing:
- `summary.json`: each step's outcome and timing
- `run.log`
- `build.log`
- `tests.log` and `tests.xml` (JUnit)
- `bench-c*.txt`
- `server.log` (Linux) or `serial.log` and `openocd.log` (a board)

The exit codes are 0 for passed, 1 for failed tests, and 2 for a build, flash or boot failure.

## Building and running the Linux build

```sh
git submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls
cmake -B build -DJS_RUNTIME=quickjs      # or jerryscript
cmake --build build -j
./build/isere                             # http://127.0.0.1:8080
```

| CMake option | Values | Default |
|---|---|---|
| `TARGET_PLATFORM` (or `ISERE_BOARD`) | `linux`, `frdm-k64f`; fixed per build directory (other ports: `PORTING.md`) | `linux` |
| `JS_RUNTIME` | `quickjs`, `jerryscript` | `quickjs` |
| `ISERE_APP_DIRS` | app directories to embed (`;`-separated); isère's own build only (the scripts add `tests/e2e/app`) | `examples/jwks/app` |
| `ISERE_BUILD_ID` | the ID served at `/__version` | the git commit; for an app, `<app commit>+isere.<isère commit>` |
| `ISERE_BOARD_ENV` | `NAME=value;...` set with `setenv()` at boot (FRDM-K64F) | empty |
| `ISERE_WITH_OTEL` | OpenTelemetry exporter (Linux only; formerly `WITH_OTEL`) | `OFF` |
| `ISERE_OTEL_HOST`, `ISERE_OTEL_PORT` | its collector (formerly `OTEL_HOST`, `OTEL_PORT`) | `127.0.0.1`, `4318` |
| `ARM_TOOLCHAIN_PATH` | `arm-none-eabi-gcc`'s directory (FRDM-K64F) | `PATH` or `.toolchain/` |
| `CMAKE_BUILD_TYPE` | | `RelWithDebInfo` |

The same options apply to an app project that includes isère (see [`examples/hello-world`](../examples/hello-world)), which also takes `ISERE_PATH` and `ISERE_FETCH_FROM_GIT_TAG` to find isère.

| Environment (Linux) | Meaning |
|---|---|
| `ISERE_CONFIG` | JSON merged over the app's `config` (passed to handlers as `env`), e.g. `{"jwks_url":"https://…"}` |
| `ISERE_JS_MODE` | `warm` (default) or `cold` |
| `ISERE_JS_BYTECODE=1` | QuickJS: compile once and load instances from bytecode |
| `ISERE_JS_LAZY=0` | load all of the prelude up front |
| `ISERE_HANDLER_TIMEOUT_MS` | handler deadline (default 5000) |
| `ISERE_HEAD_BUDGET`, `ISERE_BODY_BUDGET` | per-connection budgets (default 8 KB and 16 KB) |
| `ISERE_MAX_CONNECTIONS` | connection pool (default 12; the FRDM-K64F has 4) |
| `ISERE_HEAP_SIZE` | heap budget (default 16 MB) |
| `ISERE_CA_FILE` | CA bundle for HTTPS `fetch` |
| `ISERE_METRICS=0`, `ISERE_NDJSON=0`, `ISERE_CPU_CLOCK=thread` | metrics switches |

## The test suite

| File | Covers |
|---|---|
| `smoke.test.mjs` | `/__version`, the JS handler, 404, sequential and keep-alive requests, malformed requests, clients disconnecting mid-request, HEAD |
| `http.test.mjs` | in-place parsing (criterion 3): long header values, many headers, chunked bodies, 431/413 budgets, ambiguous framing (400), two requests in one segment, a request a few bytes at a time, memory proportional to the request |
| `routing.test.mjs` | endpoints routed by method and path, 405, exact paths, warm module state, resident memory per endpoint, a failing endpoint freeing its memory, no engine instance for unknown routes (cold mode) |
| `risky.test.mjs` | handlers that throw, never answer, loop, recurse without end, allocate a lot, return NUL bytes, bad headers or statuses, leave timers behind (after #33's findings) |
| `metrics.test.mjs` | `Server-Timing`, `/__metrics`, NDJSON, and calibration within ±10% (criterion 6) |
| `jwt.test.mjs` | JWT verification (criterion 2) against the server's own JWKS over loopback, an external JWKS over HTTP and over HTTPS: valid, bad signature, expired, malformed, unknown `kid`, rotation, cooldown, TTL, unreachable host, ≥1 KB tokens, RS256 and ES256 |
| `loopback.test.mjs` | verifying through the server's own endpoint with the connection pool full (criterion 4) |
| `cleanup.test.mjs` | one test per way a request can leave objects behind in a cold engine, plus a clean warm shutdown (criterion 10) |
| `load.test.mjs` | 1, 2 and 4 concurrent clients over a mix of endpoints, keep-alive and not |
| `endurance.test.mjs` | 45,000 requests, past where JerryScript used to hang, with the heap ending where it started |
| `leak.test.mjs` | valgrind in warm and cold mode: memory errors, and lost or retained memory that grows with the request count |

Test-only endpoints live in `tests/e2e/app/`, which both loops build in. Tests that need the Linux build skip themselves on a board: environment variables, valgrind, server output.

You can run the suite directly with Node 22 or newer:

```sh
ISERE_BUILD_DIR=build-linux-quickjs node --test --test-concurrency=1 'tests/e2e/*.test.mjs'
ISERE_URL=http://192.168.7.1:8080 node --test --test-concurrency=1 'tests/e2e/*.test.mjs'   # a running board
```

Against the Linux build, each file starts its own `isere` on port 8080 (override it with `ISERE_PORT`) and stops it with SIGINT; a non-zero exit fails the file. Set `ISERE_VALGRIND=0` to skip valgrind.

## `tools/linux.sh`

```sh
tools/linux.sh                          # QuickJS: build, 30 s fuzz, tests with valgrind, 10 s wrk per level
tools/linux.sh --runtime jerryscript
tools/linux.sh --no-valgrind --no-bench --fuzz 0   # quickest iteration
```

The build goes to `build-linux-<runtime>/`, with `app/` and `tests/e2e/app/` embedded. You need cmake, make, curl, Node 22+, valgrind, clang (for the fuzzer) and wrk. [`linux.Dockerfile`](linux.Dockerfile) has all of them, and CI uses the same list:

```sh
docker build -t isere-linux -f tools/linux.Dockerfile tools
docker run --rm -v "$PWD":/app -w /app isere-linux tools/linux.sh
```

`wrk` fails the step on any socket error or non-2xx response. Connections stay alive, so there should be none.

## Other tools

| Script | What it does |
|---|---|
| [`bench.mjs`](bench.mjs) | The benchmark (criterion 7): each engine at 1, 2 and 4 connections over hello, JWKS cache hit, cache miss and a mix, plus what metrics cost. It writes `BENCHMARK.md` tables. |
| [`startup.mjs`](startup.mjs) | Boot time and per-request phase timings for each engine and mode (criterion 10). |
| [`fuzz.sh`](fuzz.sh) | libFuzzer with ASan/UBSan on the HTTP parser. |
| [`freertos_measure.sh`](freertos_measure.sh) | The FreeRTOS tick and preemption measurements in `PORTING.md (FreeRTOS configuration)`. |

## `tools/hil.sh`

One iteration on the FRDM-K64F:
1. builds the firmware with a unique build ID, the test routes and a 1 s handler deadline (`ISERE_BOARD_ENV`, matching what the Linux test servers get)
2. halts the chip through the Kinetis MDM-AP and flashes it over SWD with OpenOCD (`target/kx.cfg`), through the board's OpenSDA CMSIS-DAP interface
3. records the board's UART output through OpenSDA's virtual COM port, on the same USB cable
4. reads the board's DHCP address from the `isere: ip A.B.C.D` line it prints (or takes `BOARD_HOST`)
5. waits until `/__version` reports the build ID from step 1
6. runs the test suite against the board
7. benchmarks with `wrk`, then checks the board still answers

The FRDM-K64F runs JerryScript: QuickJS needs more than its 256 KB of SRAM.

### FRDM-K64F setup

```
Mac ──USB── FRDM-K64F "SDA" port (power, SWD, UART at 115200)
LAN ──Ethernet── FRDM-K64F RJ45 (DHCP; the board also needs a route to an NTP server)
```

```zsh
brew install cmake node wrk open-ocd
```

The ARM toolchain is found on `PATH`, in `ARM_TOOLCHAIN_PATH`, or unpacked from Arm's release into the repository's ignored `.toolchain/` folder. Submodules: `3rdparty/FreeRTOS`, `3rdparty/jerryscript`, `3rdparty/lwip` (2.2.1) and `3rdparty/mbedtls`. NXP's SDK files the port needs are vendored in `portable/frdm-k64f/sdk/`.

On macOS, a serial port's settings reset when its last handle closes, so `hil.sh` configures the port and reads it on one handle. A board whose OpenSDA still runs the 2014 mbed interface firmware can drop off USB after a failed transfer; a replug brings it back. (DAPLink's `k20dx_frdmk64f_0x5000` builds don't start under that firmware's 2014 mbed bootloader, v0201: it takes the file, then nothing enumerates. Keep the mbed interface firmware on such a board.)

### Examples

```zsh
tools/hil.sh                                  # FRDM-K64F, JerryScript, full iteration
tools/hil.sh --no-bench                       # build, flash and tests only
BOARD_HOST=10.0.0.46 tools/hil.sh --skip-flash  # re-test the firmware already on the board
```
