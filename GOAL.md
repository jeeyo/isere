# Goal

**Prove that a Raspberry Pi Pico 2 running isère can host a small, real-world multi-endpoint service — JWT verification against a cached JWKS, plus our own well-known JWKS endpoint — reliably, with live per-request CPU and memory metrics and published benchmarks.**

isère is a personal project. Its purpose is to show that a microcontroller can satisfy real small use cases, not to be a general-purpose platform. Everything below serves that demonstration.

## Working rules

- **This branch is not blocked by any other PR.** Other PRs, such as [jeeyo/isere#33](https://github.com/jeeyo/isere/pull/33) (tests and findings) and [jeeyo/isere#30](https://github.com/jeeyo/isere/pull/30) / [jeeyo/isere#31](https://github.com/jeeyo/isere/pull/31) (Zephyr/Rust), are references to learn from. Everything needed for this goal is implemented on this branch, and no milestone waits for another PR to merge.
- **QuickJS cleanup comes first.** No new feature work starts until it is fixed (M2).
- **Both JS engines stay.** QuickJS and JerryScript are both kept, built and tested. Fixes are required for both, including the QuickJS cleanup and startup work (criterion 10) and the JerryScript hang under load.

## Reference service

One Pico 2 (non-W, networked over USB: CDC-ECM on macOS, RNDIS on Windows) serves at least these endpoints:

| Endpoint | Behaviour |
|---|---|
| `GET /.well-known/jwks.json` | Serves our own public keys (static, from configuration). |
| `GET /verify` (protected) | Reads `Authorization: Bearer <jwt>`, resolves the key by `kid` from a JWKS cached in memory (TTL), verifies the signature, returns `200` with the claims or `401`. |
| `GET /__metrics` | Live device and per-endpoint metrics (see below). |

The verifier's JWKS URL is configurable and must work against both:

- **our own endpoint** on the same board (plain HTTP, loopback). This is the repeatable benchmark fixture.
- **an external well-known JWKS over HTTPS**, reached through the USB host. This is the real-world demo.

## Definition of done

All criteria are checked for both JS engines: QuickJS (quickjs-ng) and JerryScript. Each one passes on the Linux build first (`tools/linux.sh`) and is then confirmed on a real Pico 2 (`tools/hil.sh`).

1. **Multiple endpoints**
   - Endpoints are defined independently and routed by method and path inside isère. There is no single catch-all handler.
   - Unknown routes return `404` without touching any JS engine.
   - A throwing, slow or timed-out endpoint does not affect requests to other endpoints. It returns `5xx` and frees its memory.
   - The resident memory cost of each endpoint is measured and reported.

2. **JWT verification works**, with tests for:
   - a valid token
   - a bad signature
   - an expired token
   - a malformed token
   - an unknown `kid`, which triggers exactly one JWKS refetch
   - an unreachable JWKS host while the cache is still fresh, which still verifies
   - N requests within the TTL, which cause exactly one JWKS fetch
   - RS256 is required and ES256 is a stretch goal.
   - Realistic token sizes (≥ 1 KB `Authorization` header) are accepted.

3. **HTTP is parsed in place, safely**
   - Each connection receives bytes directly into its own buffer, and the request is parsed there.
   - The method, path, headers and body are stored as `{offset, len}` spans into that buffer. Nothing in the request path copies them into fixed-size fields.
   - There are no per-field limits. The only limits are two configurable budgets per connection: one for the request head and one for the body. Exceeding them returns `431` or `413`.
   - From the receive buffer to the JS engine, the request is copied exactly once, into the engine's own strings, using length-aware calls. Nothing in the request path uses `strlen`, `strcpy`, `strncpy` or `strdup`.
   - Two requests arriving in one TCP segment are parsed one after another from the same buffer.
   - Ambiguous framing is rejected with `400`: both `Content-Length` and `Transfer-Encoding`, or duplicate `Content-Length` headers.
   - The parser is fuzzed with AddressSanitizer in CI on the Linux build. The same C code runs on the Pico 2.
   - The memory each connection uses is measured. It is proportional to the actual request, not to a worst-case layout.

4. **The board can verify tokens using its own endpoint**
   - The verifier can fetch the JWKS from the same board while the board is serving other requests.
   - It does not deadlock or run out of connections when the connection pool is full.

5. **Per-request metrics, live**
   - Every request reports:
     - wall time
     - CPU time spent executing that request's code (time waiting on I/O and timers is excluded)
     - bytes allocated
     - peak heap during the request
     - net heap change after the request completes
   - Each request exposes its metrics in a `Server-Timing` response header.
   - Device-wide metrics are CPU utilisation, free heap, and the lowest free heap so far.
   - Per-endpoint aggregates (count, p50/p99 latency, mean CPU, mean allocation) are reported.
   - All of these are exposed live via `/__metrics` and as NDJSON over serial.

6. **The metrics are shown to be accurate**
   - Calibration endpoints with known costs (≈5 ms CPU busy-loop, 32 KB allocate/free, 100 ms sleep) report within ±10% of the expected value.
   - The sleep endpoint reports CPU time ≈ 0.
   - Enabling metrics costs < 5% throughput.

7. **The benchmarks are honest and repeatable**
   - One script benchmarks:
     - each engine
     - concurrency 1, 2 and 4 (the board's real connection limit)
     - JWKS cache hit and cache miss
     - a mix of requests across endpoints
   - Each run lasts 10 minutes.
   - Reported figures: req/s, p50/p99 latency, per-request CPU and heap from criterion 5, and the lowest free heap.
   - The same script runs against the Linux build for comparison.
   - Results are published in `BENCHMARK.md` with the commit hash.

8. **It's reliable**
   - The 10-minute runs have zero crashes and zero socket errors.
   - Over 10k+ requests, the net heap change per request stays at about zero, with both engines.
   - On the Linux build, under valgrind, with both engines:
     - zero invalid reads, writes or frees
     - no growth in lost memory between a short and a long run
     - no more than 8 bytes of retained memory per request
   - The tests cover the JS handler, the built-in endpoints and the 404 path.

9. **FreeRTOS configuration is understood and documented**
   - Every setting in `portable/*/include/FreeRTOSConfig.h` is listed in `docs/FREERTOS_CONFIG.md`.
   - Each setting is classified as one of:
     - **essential**: isère does not build or work correctly without this value. For example, `configUSE_MUTEXES` is needed by the logger. The doc says what breaks without it.
     - **effective**: a tunable value that measurably changes performance or memory, such as tick rate, task stack sizes, timer queue length or priorities. The doc records the chosen value and the measurement behind it (Linux and Pico 2 numbers from the validation loops).
     - **default**: should stay at FreeRTOS's default. The setting is removed from the file, or kept only with a comment saying why.
   - Differences between the Linux and Pico 2 configs are explained, not accidental.

10. **QuickJS cleanup is fixed and its startup cost is understood**
    - **Cleanup is fixed.** Creating and freeing a QuickJS runtime and context:
      - never trips the `JS_FreeRuntime` `gc_obj_list` assertion
      - leaves no objects behind
      - passes the valgrind growth checks, including requests that leave timers or promises pending
      - Each cause of a leftover object has its own test.
    - **Startup time is measured inside isère**, with timestamps around each phase, on Linux and on the Pico 2, for both engines:
      - process or firmware boot to first request served
      - per request: runtime creation, context creation, polyfill setup, handler module evaluation, the handler call, and teardown
    - **Today's numbers (Linux, before any fixes):**
      - Both engines boot in about 12 ms.
      - Sequential requests take a p50 of about 2.15 ms on both. Fixed costs, such as the event loop's 5 ms poll, hide the engine's share.
      - Under `wrk`, JerryScript serves about 3× as many requests per second (about 2,000 vs 650 on this machine; 3,720 vs 300 in `BENCHMARK.md`).
      - Hence the phase timings above.
    - **QuickJS's startup cost is brought to JerryScript's level, or the remaining gap is explained and accepted.** Options to try, measured with the phase timings:
      - a warm runtime, so only a context is created per request
      - precompiled handler bytecode (`JS_WriteObject`/`JS_ReadObject`) instead of parsing source on each request
      - lazy polyfill setup
    - Both engines keep running the same tests and benchmarks throughout.

11. **The platform decision rests on data**
   - FreeRTOS/C and Zephyr/Rust are evaluated against the same black-box test suite and benchmark script.
   - Whichever stack is chosen must own its event loop (scheduling) and HTTP parsing.
   - The decision and its numbers are written down.

## Known gaps on `main` (as of this writing)

- **The build is fragile.**
  - Build directories must sit directly under the repo, because `CMakeLists.txt` reads `../js/handler.js`.
  - CI runs a `unittests` target that no longer exists.
- **quickjs-ng on the Pico 2 runs without computed-goto dispatch.**
  - It is built with `EMSCRIPTEN` defined, which turns off pthreads and C11 atomics, as the old QuickJS build also did.
  - The same define also turns off computed-goto dispatch, which costs interpreter speed.
  - Worth measuring once the benchmarks run on hardware.
- **The HTTP server has open bugs.** These were found by [jeeyo/isere#33](https://github.com/jeeyo/isere/pull/33):
  - a response body containing a NUL leaks adjacent heap memory to the client
  - `ISERE_HTTPD_HANDLER_TIMEOUT_MS` is defined but never used
  - the wait loop busy-waits without yielding to other tasks
- **Only one handler is supported.**
  - The loader loads one source file.
  - That file is re-evaluated on every request (`src/http_handler.c`).
  - There is no router and no state that persists between requests.
- **Header values are truncated to 63 characters.**
  - `src/http_handler.c` copies header values with `ISERE_HTTPD_MAX_HTTP_HEADER_NAME_LEN`.
  - `ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN` is 512 anyway.
  - Either limit breaks real JWTs.
- **Every request is copied about three times.**
  - The server reads 64-byte chunks, and llhttp `strncpy`s them into fixed per-connection fields: method 16, path 256, 16 × (64 + 512) for headers, and 512 for the body. That reserves about 10 KB per connection whatever the request.
  - `src/http_handler.c` copies the headers again onto the stack (about 9 KB).
  - The values are then `strdup`ed on their way into the engine.
- **There's no outbound HTTP or TLS.**
  - `fetch` is a stub (`src/polyfills/quickjs/fetch.c`).
  - There's no TLS client.
  - The `crypto-module` branch has only a random-number generator.
- **The Pico 2 has no route off the USB link.**
  - The board needs a default gateway and DNS pointing at the host.
  - The host needs IP forwarding and NAT.
  - Fixing this also unblocks exporting to an OpenTelemetry collector.
- **The QuickJS engine leaks on cleanup.** This is the first thing to fix.
  - A runtime is created and freed per connection.
  - `JS_FreeRuntime` used to trip the `gc_obj_list` assertion when two requests were parsed into one connection. That path is fixed, but the cleanup itself still needs checking.
  - The cases most likely to leave objects behind are timers and promises still pending at teardown, polyfill globals, and handler exceptions.
- **QuickJS handles fewer requests per second than JerryScript** under `wrk`: about 3× fewer. See criterion 10.
- **There are no per-request metrics.**
  - Only process-level OpenTelemetry counters and gauges exist.
- **JerryScript hangs under `wrk`.**
  - After roughly 29,000 `wrk` requests, the process stays alive with one thread spinning but stops accepting connections.
  - The log stops partway through a handler's `console.log`.
  - QuickJS doesn't hang. Neither do Node clients at the same rate, with or without keep-alive. Pipelining is ruled out as the cause.
  - The `wrk` step in `tools/linux.sh` catches it. The tests don't yet.
  - The bug is already on `main`.
- **Every response counts as a socket error in `wrk`.**
  - Responses are sent with `Connection: close`, and `wrk` counts each close as a read error.
  - "Zero socket errors" therefore needs keep-alive support, or a benchmark that counts closed connections separately.

## Design directions

These are directions, not decisions.

- **In-place parser**
  - Use a stateless parser in the style of [picohttpparser](https://github.com/h2o/picohttpparser).
    - On each `recv`, it re-parses the request head from the start of the connection buffer and returns pointer and length spans. This costs very little for a few KB.
    - It is about 700 lines of C, it decodes chunked bodies in place, and it is the design that Rust's `httparse` copies. That keeps the C stack and the Zephyr/Rust spike comparable.
  - Alternatively, keep llhttp but feed it from the contiguous connection buffer. Its callbacks then already point into that buffer, and a field split across two reads can be merged by extending the span.
  - Store offsets, not pointers, so the buffer can grow by `realloc` up to its budget.
  - The spans stay valid until the response is sent.
  - The connection is never freed from inside a parser callback.

- **Warm runtime**
  - Keep one long-lived runtime and a module instance per endpoint.
  - Give each request a fresh call, not a fresh runtime.
  - This makes the JWKS cache possible and removes per-request runtime teardown from the hot path.
- **Native crypto**
  - Use a small `crypto.subtle` subset (`importKey('jwk')`, `verify`) backed by mbedTLS, which pico-sdk already vendors.
  - Use the same mbedTLS for TLS in `fetch`.
  - Follow the Cloudflare Workers API shape so handlers stay portable.
- **Per-request CPU accounting**
  - Measure each stretch where a request's JS runs (handler call, promise jobs, timer callbacks) with the cycle counter, and add them up.
  - Count allocations per request through the engine's allocator hooks.
  - JerryScript may need its own hook, and its per-request memory figures may stay approximate at first.
- **Memory budget**
  - The RP2350 has 520 KB of SRAM, and a TLS handshake needs roughly 40–60 KB next to the engine and N endpoints.
  - The "lowest free heap so far" metric is the evidence for whether this fits.

## Validation loops

Both loops run the same black-box test suite: `tests/e2e/*.test.mjs`, written with Node's built-in `node:test` runner and needing no dependencies. Work on the Linux build until it's green, then confirm on hardware.

### Linux first: `tools/linux.sh`

1. **Build** the Linux binary with a unique build ID.
2. **Run** the test suite. Each test file starts its own server, waits for `/__version`, and stops it with SIGINT. A server that ignores SIGINT counts as hung.
3. **Check memory with valgrind** (`tests/e2e/leak.test.mjs`).
   - The server runs twice under valgrind, once with 20 requests and once with 120, mixing `/`, `/__version` and the 404 path.
   - The run fails on any invalid read, write or free.
   - It also fails on any growth in lost memory between the two runs, or more than 8 bytes of retained memory per request.
   - isère exits without freeing its long-lived state, so the test compares two runs instead of reading one report. That separates per-request leaks from start-up allocations.
4. **Benchmark** with `wrk` at 1, 2 and 4 connections, then check the server still answers.

One iteration takes about a minute, most of it spent in valgrind.

### Then hardware: `tools/hil.sh`

This runs on the developer's Mac. A Raspberry Pi Debug Probe is attached to the Pico 2 over SWD and UART, and the Pico 2 is attached to the Mac over USB for networking. Setup is in `tools/README.md`.

1. **Build** the firmware, stamping it with a unique build ID (git hash, a dirty flag and a timestamp).
2. **Flash** it over SWD with `openocd … program isere.elf verify reset`. This needs no BOOTSEL button and no drive copy, and it works even if the previous build crashed.
3. **Capture** the board's UART output through the probe from the moment it boots.
4. **Wait** for the board's network interface, identified by MAC `02:02:84:6a:96:00` because interface names vary, to get an IPv4 address from the board's DHCP server.
5. **Wait until the board is ready**: `GET /__version` must return the build ID from step 1. This proves the board isn't running stale firmware.
6. **Run** the same test suite against the board (`ISERE_URL`). The valgrind tests skip themselves on hardware.
7. **Benchmark** the board, then check it still answers.

### Both loops

- Each run writes `hil-results/<build-id>/` with `summary.json`, the logs, `tests.log` and `tests.xml` (JUnit), and the benchmark output.
- The exit codes are 0 for passed, 1 for failed tests, and 2 for build, flash or boot problems, so a local agent can run either loop repeatedly.
- `tools/hil.sh` needs the physical Mac, so it can't run in a cloud session. `tools/linux.sh` runs anywhere.

## Milestones

Status (Linux): M2–M6 are done and green on the Linux loop for both engines (123 tests including valgrind, fuzzing, wrk), and `BENCHMARK.md` holds the 10-minute Linux runs at commit `281f738`: 12.3M (QuickJS) and 7.1M (JerryScript) requests with zero errors and no heap growth. Pico 2 confirmation is still open for every milestone. M7 is set aside: the Zephyr/Rust spike is vendored in `spikes/zephyr-rust/` and builds for `native_sim`, but its JS path aborts in `JS_FreeRuntime` on the first request, and `docs/PLATFORM_DECISION.md` is not written.

- [ ] **M1 Baseline**
  - [x] Move to quickjs-ng (the port adaptation follows #33's approach) and build it for the Pico 2.
  - [x] Build the hardware validation loop: UART logging on the Pico 2, a build ID and `/__version` endpoint, and `tools/hil.sh` flashing through the Debug Probe.
  - [x] Build the Linux validation loop: the `node:test` suite, valgrind memory checks, and `tools/linux.sh`.
  - [x] Stop freeing the connection inside the llhttp callback. This fixes the crash on two requests sent together, and valgrind now reports 0 memory errors.
  - [x] Write the remaining risky-handler cases on this branch in `node:test`, using #33's findings as a reference. Some need test-specific handlers, such as throwing, NUL bodies and timers.
  - [x] Run `tools/linux.sh` in CI, replacing the dead `unittests` step.
  - [ ] Run the hardware loop for the first time on real hardware.
  - [ ] Record today's Pico 2 numbers through the loop.
- [ ] **M2 QuickJS cleanup and startup first**
  - Add per-phase startup timing (criterion 10), and record the QuickJS and JerryScript baselines on Linux and the Pico 2.
  - Fix the QuickJS runtime and context cleanup. Each leftover-object cause gets a failing test first, and both loops must be green afterwards.
  - Reduce QuickJS's startup cost: warm runtime, precompiled bytecode, lazy polyfills. Measure each change.
- [ ] **M3 Correct core**
  - Replace the HTTP request path with in-place parsing, and add the fuzzer. This structurally removes the header truncation and the per-field limits. Applying the same length-aware rule to the response path, instead of `strdup`, removes the NUL-body leak.
  - Fix the handler timeout and the busy-wait loop, plus the JerryScript hang under `wrk`.
  - Write `docs/FREERTOS_CONFIG.md` (criterion 9), using the loops to measure the settings classified as effective.
  - Each fix starts with a failing test, and every fix is validated on Linux first and then on the Pico 2.
- [ ] **M4 Multi-endpoint**
  - Add the router, warm runtime, per-endpoint isolation and timeouts, and serve `/.well-known/jwks.json`.
- [ ] **M5 Metrics**
  - Add per-request CPU and heap accounting, `Server-Timing`, `/__metrics`, serial NDJSON, and the calibration endpoints.
- [ ] **M6 Outbound + crypto**
  - Add `fetch` over HTTP then HTTPS, mbedTLS `crypto.subtle.verify`, host routing for the Pico 2, and the JWKS cache.
- [ ] **M7 Zephyr/Rust spike** (time-boxed)
  - Build an equivalent with its own event loop and HTTP parser.
  - Run the same tests and benchmarks.
- [ ] **M8 Decide and publish**
  - Choose the stack and publish the final `BENCHMARK.md` with results on Pico 2.
