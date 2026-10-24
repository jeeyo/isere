# Goal

**Prove that a Raspberry Pi Pico 2 running isère can host a small, real-world multi-endpoint service — JWT verification against a cached JWKS, plus our own well-known JWKS endpoint — reliably, with live per-request CPU and memory metrics and published benchmarks.**

isère is a personal project. Its purpose is to show that a microcontroller can satisfy real small use cases, not to be a general-purpose platform. Everything below serves that demonstration.

## Reference service

One Pico 2 (non-W, networked over USB RNDIS) serves at least these endpoints:

| Endpoint | Behaviour |
|---|---|
| `GET /.well-known/jwks.json` | Serves our own public keys (static, from configuration). |
| `GET /verify` (protected) | Reads `Authorization: Bearer <jwt>`, resolves the key by `kid` from a JWKS cached in memory (TTL), verifies the signature, returns `200` with the claims or `401`. |
| `GET /__metrics` | Live device and per-endpoint metrics (see below). |

The verifier's JWKS URL is configurable and must work against both:

- **our own endpoint** on the same board (plain HTTP, loopback). This is the repeatable benchmark fixture.
- **an external well-known JWKS over HTTPS**, reached through the USB host. This is the real-world demo.

## Definition of done

All criteria are checked on a real Pico 2, for both QuickJS and JerryScript unless stated otherwise.

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

9. **The platform decision rests on data**
   - FreeRTOS/C and Zephyr/Rust are evaluated against the same black-box test suite and benchmark script.
   - Whichever stack is chosen must own its event loop (scheduling) and HTTP parsing.
   - The decision and its numbers are written down.

## Known gaps on `main` (as of this writing)

- **The build doesn't work from a clean clone.**
  - The `3rdparty/quickjs` submodule URL returns 404.
  - CI runs a `unittests` target that no longer exists.
  - [jeeyo/isere#33](https://github.com/jeeyo/isere/pull/33) fixes both and adds a black-box test suite.
- **The HTTP server has open bugs.** These were found by #33:
  - two requests sent in one TCP segment crash the server (use-after-free)
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
- **The QuickJS engine leaks on cleanup.**
  - A runtime is created and freed per connection, and `JS_FreeRuntime` trips the `gc_obj_list` assertion.
- **There are no per-request metrics.**
  - Only process-level OpenTelemetry counters and gauges exist.
- **The Pico 2 produces no logs.**
  - The logger is a no-op (`portable/pico2/src/logger.c`).
  - stdio is set to semihosting, which needs a debugger attached.
- **There is no software path back to BOOTSEL.**
  - The USB descriptor exposes only RNDIS. There is no picotool reset interface.
  - Every reflash therefore needs the BOOTSEL button pressed by hand.

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

## Hardware validation loop

Every Pico 2 iteration runs this loop on the developer PC, with the board attached by USB:

1. **Build** the UF2 with `TARGET_PLATFORM=pico2`, stamping it with a build ID (git hash plus a counter).
2. **Get the board into BOOTSEL without touching it.** Try these in order:
   - **Debug Probe (recommended)**
     - A Raspberry Pi Debug Probe on SWD flashes and resets through `openocd`, so no BOOTSEL or drive copy is needed.
     - It works even after the firmware has crashed.
     - It also provides the UART for logs and GDB for crash backtraces.
     - With a probe, steps 3–4 are replaced by `openocd … program isere.elf verify reset`.
   - **`POST /__bootsel`**
     - A dev-build endpoint that calls `reset_usb_boot()`.
   - **On a crash**
     - In dev builds, the HardFault handler and a watchdog timeout reboot the board into BOOTSEL.
     - This means a crashing build never needs a hand on the button.
   - **Last resort: a human holds BOOTSEL and replugs the board.**
     - The script detects this case and asks for it, rather than hanging.
3. **Wait for the drive** (label `RP2350`) to mount, with a timeout. Copy the UF2 and `sync`.
4. **Wait for the board to reboot.** The drive should disappear, then the RNDIS interface should appear. Identify the interface by its MAC address `02:02:84:6A:96:00`, not by its name, since names differ between OSes.
5. **Wait until the board is ready.** That means `GET http://192.168.7.1:8080/__version` returns the build ID from step 1. Interface-up is not enough, and the ID check proves the board isn't running stale firmware.
6. **Run the end-to-end test suite and the benchmark** against `192.168.7.1`. Capture the device logs (UART or NDJSON) next to the results.
7. **Record everything.** Write a results file with the build ID, timings for each step, pass/fail, benchmark numbers and the log path. Any step that times out fails the iteration with a clear reason.

The loop is one script (e.g. `tools/hil.sh`) that exits non-zero on failure, so a local agent can run it in a loop.

It needs the physical PC, so it can't run in a cloud session. Run it from a local Claude Code session, or register the PC as a self-hosted GitHub Actions runner to get hardware-in-the-loop CI.

## Milestones

- [ ] **M1 Baseline**
  - Merge #33 and get a clean-clone build and green CI.
  - Write the benchmark script.
  - Build the hardware validation loop: logging on the Pico 2, a `/__version` endpoint, a software path back to BOOTSEL, and `tools/hil.sh`.
  - Record today's Pico 2 numbers through the loop.
- [ ] **M2 Correct core**
  - Replace the HTTP request path with in-place parsing, and add the fuzzer. This structurally removes the crash on two requests sent together, the NUL-body leak and the header truncation.
  - Fix the timeout, the busy-wait loop and the QuickJS cleanup leak.
  - Each fix starts with a failing test, and every fix is validated on the Pico 2 through the loop.
- [ ] **M3 Multi-endpoint**
  - Add the router, warm runtime, per-endpoint isolation and timeouts, and serve `/.well-known/jwks.json`.
- [ ] **M4 Metrics**
  - Add per-request CPU and heap accounting, `Server-Timing`, `/__metrics`, serial NDJSON, and the calibration endpoints.
- [ ] **M5 Outbound + crypto**
  - Add `fetch` over HTTP then HTTPS, mbedTLS `crypto.subtle.verify`, host routing for the Pico 2, and the JWKS cache.
- [ ] **M6 Zephyr/Rust spike** (time-boxed)
  - Build an equivalent with its own event loop and HTTP parser.
  - Run the same tests and benchmarks.
- [ ] **M7 Decide and publish**
  - Choose the stack and publish the final `BENCHMARK.md` with results on Pico 2.
