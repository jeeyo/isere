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

3. **The board can verify tokens using its own endpoint**
   - The verifier can fetch the JWKS from the same board while the board is serving other requests.
   - It does not deadlock or run out of connections when the connection pool is full.

4. **Per-request metrics, live**
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

5. **The metrics are shown to be accurate**
   - Calibration endpoints with known costs (≈5 ms CPU busy-loop, 32 KB allocate/free, 100 ms sleep) report within ±10% of the expected value.
   - The sleep endpoint reports CPU time ≈ 0.
   - Enabling metrics costs < 5% throughput.

6. **The benchmarks are honest and repeatable**
   - One script benchmarks:
     - each engine
     - concurrency 1, 2 and 4 (the board's real connection limit)
     - JWKS cache hit and cache miss
     - a mix of requests across endpoints
   - Each run lasts 10 minutes.
   - Reported figures: req/s, p50/p99 latency, per-request CPU and heap from criterion 4, and the lowest free heap.
   - The same script runs against the Linux build for comparison.
   - Results are published in `BENCHMARK.md` with the commit hash.

7. **It's reliable**
   - The 10-minute runs have zero crashes and zero socket errors.
   - Over 10k+ requests, the net heap change per request stays at about zero, with both engines.

8. **The platform decision rests on data**
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

## Design directions

These are directions, not decisions.

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

## Milestones

- [ ] **M1 Baseline**
  - Merge #33 and get a clean-clone build and green CI.
  - Write the benchmark script.
  - Record today's Pico 2 numbers.
- [ ] **M2 Correct core**
  - Fix the four #33 bugs, the header truncation and the QuickJS cleanup leak, each with a failing-first test.
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
