# Validation loops

Two scripts run the same black-box test suite, `tests/e2e/*.test.mjs`. It's written with Node's built-in [`node:test`](https://nodejs.org/api/test.html) runner and has no dependencies.

| Script | Target | Use it |
|---|---|---|
| `tools/linux.sh` | Linux build on this machine | first, on every change: about three minutes per iteration, including fuzzing and valgrind |
| `tools/hil.sh` | Raspberry Pi Pico 2 through a Debug Probe | once the Linux loop is green |

Both write `hil-results/<build-id>/`, containing:
- `summary.json`: each step's outcome and timing
- `run.log`
- `build.log`
- `tests.log` and `tests.xml` (JUnit)
- `bench-c*.txt`
- `server.log` (Linux) or `serial.log` and `openocd.log` (Pico 2)

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

One iteration:
1. builds the firmware with a unique build ID
2. flashes it over SWD with a Raspberry Pi Debug Probe (no BOOTSEL button, works even if the previous build crashed)
3. records the board's UART output through the probe
4. waits for the board's USB network interface (MAC `02:02:84:6a:96:00`) to get an IPv4 address
5. waits until `http://192.168.7.1:8080/__version` reports the build ID from step 1, so it can't test stale firmware
6. runs the test suite against the board
7. benchmarks with `wrk` at 1, 2 and 4 connections, then checks the board still answers

### Wiring

```
Mac ──USB── Debug Probe ──"D" (SWD) cable── Pico 2 SWD header (SWCLK / GND / SWDIO)
                        └─"U" (UART) cable── Pico 2 GP0 (UART0 TX) → probe RX
                                             Pico 2 GP1 (UART0 RX) ← probe TX
                                             GND ── GND
Mac ──USB── Pico 2 (power + CDC-ECM network, 192.168.7.1)
```

### macOS setup

```zsh
brew install cmake node wrk
```

You also need:
- **ARM toolchain.** Either `brew install --cask gcc-arm-embedded`, or the toolchain the Raspberry Pi Pico VS Code extension installs under `~/.pico-sdk/toolchain/`. If you use the extension's toolchain, set `PICO_TOOLCHAIN_PATH` to it.
- **openocd with RP2350 support.** That means the Raspberry Pi fork, which the Pico VS Code extension installs under `~/.pico-sdk/openocd/<version>/openocd`. `hil.sh` finds it there automatically. Any other build works if it has `target/rp2350.cfg`; point `OPENOCD` at it. The Homebrew `open-ocd` release may not have RP2350 support yet.
- **Submodules.** Run `git submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/llhttp 3rdparty/libyuarel 3rdparty/pico-sdk`, then `git -C 3rdparty/pico-sdk submodule update --init lib/tinyusb lib/lwip`.

On Apple silicon Macs, macOS may ask "Allow accessory to connect?" the first time each USB device appears. Allow both the probe and the Pico 2.

If more than one `/dev/cu.usbmodem*` device exists, set `HIL_SERIAL` to the probe's UART.

### Examples

```zsh
tools/hil.sh                      # full iteration, QuickJS
tools/hil.sh --runtime jerryscript
tools/hil.sh --no-bench           # build, flash and tests only
tools/hil.sh --skip-flash         # re-test the firmware already on the board
```
