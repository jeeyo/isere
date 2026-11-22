# Benchmark

Linux build at commit `281f738` with both JS engines, and the NXP FRDM-K64F board with JerryScript (below). Pico 2 figures will be added from the same scripts through `tools/hil.sh --board pico2`.

- **Host:** Apple-silicon Mac, Docker (colima, 6 vCPUs, 8 GB), Ubuntu 24.04 aarch64 (`tools/linux.Dockerfile`).
- **Server:** `RelWithDebInfo`, warm engines, defaults (16 MB heap budget, 12 connections, 100 Hz tick), `ISERE_NDJSON=0`.
- **Client:** [`tools/bench.mjs`](tools/bench.mjs) in the same container, one request in flight per connection, a fresh server per cell.
- **Duration:** 12 cells of 50 s per engine (about 10 minutes), then the metrics-overhead check.

| Scenario | Each request |
|---|---|
| hello | `GET /`: the smallest JS handler |
| jwks-hit | `GET /verify` with an RS256 token, JWKS cached |
| jwks-miss | the same with `jwks_ttl_ms: 0`: the JWKS is fetched from this server over loopback first |
| mix | in turn: `/`, `/verify`, `/.well-known/jwks.json` (static), `/__version`, a 404 |
| builtin | `GET /__version`: no JS |

## FRDM-K64F

The first numbers from real hardware. JerryScript only: QuickJS needs more than the board's 256 KB of SRAM.

- **Board:** NXP FRDM-K64F, Cortex-M4F at 120 MHz, FreeRTOS `ARM_CM4F`, lwIP 2.2.1 over the on-board 100 Mbit/s Ethernet, `RelWithDebInfo` with GCC 14.2.
- **Memory:** 217 KB of heap (all of SRAM_L and the rest of SRAM_U), of which JerryScript's arena takes 96 KB; 4 connections; NDJSON per-request lines on the UART.
- **Client:** [`tools/bench.mjs --url`](tools/bench.mjs) on the Mac, over Wi-Fi to the LAN (ping 3–5 ms), one request in flight per connection, 50 s per cell. The JWKS-miss cells need a run-time config change a board can't take.

| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | heap Δ/req after GC | min free heap | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| hello | 1 | 106 | 7.76 ms | 39.66 ms | 2.99 ms | 296 B | 105 KB | 0.000 B | 94 KB | 0 |
| hello | 2 | 219 | 8.14 ms | 23.29 ms | 3.05 ms | 296 B | 106 KB | 0.000 B | 94 KB | 0 |
| hello | 4 | 263 | 15.08 ms | 23.77 ms | 3.08 ms | 296 B | 109 KB | 0.000 B | 94 KB | 0 |
| jwks-hit | 1 | 10 | 95.5 ms | 99.7 ms | 89.9 ms | 8087 B | 111 KB | 0.000 B | 94 KB | 0 |
| jwks-hit | 2 | 11 | 181.7 ms | 204.4 ms | 90.1 ms | 8087 B | 113 KB | 0.000 B | 94 KB | 0 |
| jwks-hit | 4 | 11 | 273.1 ms | 547.5 ms | 90.1 ms | 8087 B | 116 KB | 0.000 B | 92 KB | 0 |
| mix | 1 | 42 | 6.18 ms | 98.3 ms | 18.5 ms | 1900 B | 111 KB | 0.000 B | 92 KB | 0 |
| mix | 2 | 48 | 7.44 ms | 183.1 ms | 18.6 ms | 2047 B | 113 KB | 0.000 B | 92 KB | 0 |
| mix | 4 | 51 | 93.0 ms | 278.1 ms | 18.6 ms | 2071 B | 118 KB | 0.013 B | 89 KB | 0 |

**20-minute soak** (mix at 2 connections, `/__metrics` sampled every 30 s): 48 req/s, p50 7.2 ms, 0 errors and 0 engine restarts. Free heap held at 99,868 bytes from the second minute to the last, and its lowest mark (91,603) never moved; JerryScript's heap stayed between 25.8 and 32.5 KB, a GC sawtooth. The board's own per-route latency stayed at p50 3.3 ms for `/` and 90.1 ms for `/verify` throughout.

**Repeatability:** a second run of the whole matrix on the same firmware matched the first: CPU per request within 0.3% in every cell, p50 within 0.5 ms, the lowest free heap identical, 0 errors again. The p99 at four connections repeated almost exactly (jwks-hit 547 → 550 ms, mix 278 → 279 ms), so it is a fixed queueing pattern of the saturated board, not jitter. The only cells that moved were hello's p99 at one and two connections (39.7 → 13.4 ms, 23.3 → 13.5 ms), with unchanged p50 and CPU: tail noise from the Wi-Fi client.

**Garbage collection doesn't stutter.** JerryScript collects whenever its heap has grown by `JERRY_GC_LIMIT`, which defaults to 1/32 of the arena: 3 KB here. A request allocates more than that (an RS256 verification about 8 KB), so collection runs on nearly every request and its cost, marking the same 25–32 KB of live objects, is spread evenly instead of landing on every Nth request. Measured on the board, one request in flight at a time: 1,500 hellos took 3.48 ms of server CPU at p50 and 3.55 ms at p99, max 3.57 ms; 400 RS256 verifications took 90.85 ms at p50 and 91.2 ms at p99, with one outlier at 104 ms (the first, most likely refetching the JWKS after its 5-minute TTL). A larger `JERRY_GC_LIMIT` would collect less often but pause longer when it does.

**The client-side latency tail is the Wi-Fi.** The p99 of a hello at one connection (13–40 ms across runs, against a 3.5 ms server-side wall time) comes from bursts of 30–290 ms delays that also appear when pinging the idle board and the LAN's router from the same Mac (router: p50 4.6 ms, p99 118 ms). The board's own wall time stays at 3.5 ms on those requests.

**How to read it:** the board is CPU-bound. A JS hello costs 3 ms of CPU and an RS256 verification 90 ms, so throughput stops growing once the CPU is busy (about 330 hellos or 11 verifications a second) and more connections only queue. CPU per request is the same at every concurrency level. Latency at one connection is the CPU time plus the network: `/__version`, which runs no JS, answers in 4.4 ms on keep-alive, about one Wi-Fi round trip.

**Before the UART fix**, every request also waited for its NDJSON line to be written to the 115200-baud UART, about 11 ms per line with polled writes: hello at one connection measured 46 req/s and p50 21 ms. The UART is now drained by its interrupt from a 4 KB queue (`portable/frdm-k64f/src/board.c`).

## Reliability

| | QuickJS | JerryScript |
|---|---:|---:|
| requests over the 10-minute run | 12,336,255 | 7,063,209 |
| errors (wrong status, dropped connection, socket error) | 0 | 0 |
| crashes or engine restarts | 0 | 0 |
| worst heap change per request after GC | -0.018 B | 0.000 B |

## Keep-alive

### quickjs

| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | net/req (Server-Timing) | heap Δ/req after GC | min free heap | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| hello | 1 | 16301 | 0.059 ms | 0.132 ms | 8.4 µs | 4296 B | 367 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |
| hello | 2 | 38282 | 0.057 ms | 0.117 ms | 6.4 µs | 4296 B | 368 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |
| hello | 4 | 97886 | 0.031 ms | 0.171 ms | 4.7 µs | 4296 B | 370 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |
| jwks-hit | 1 | 5276 | 0.181 ms | 0.301 ms | 127.0 µs | 93120 B | 419 KB | 256.0 B | 0.000 B | 15957 KB | 0 |
| jwks-hit | 2 | 7644 | 0.250 ms | 0.420 ms | 120.8 µs | 93120 B | 424 KB | 256.0 B | 0.000 B | 15954 KB | 0 |
| jwks-hit | 4 | 7629 | 0.401 ms | 0.979 ms | 121.9 µs | 93120 B | 443 KB | 256.0 B | 0.000 B | 15938 KB | 0 |
| jwks-miss | 1 | 4334 | 0.221 ms | 0.341 ms | 158.1 µs | 100833 B | 426 KB | 3044.0 B | 0.000 B | 15957 KB | 0 |
| jwks-miss | 2 | 4323 | 0.221 ms | 0.347 ms | 158.2 µs | 98391 B | 432 KB | 3043.9 B | 0.000 B | 15950 KB | 0 |
| jwks-miss | 4 | 4312 | 0.222 ms | 0.341 ms | 158.8 µs | 96745 B | 457 KB | 3043.8 B | -0.018 B | 15927 KB | 0 |
| mix | 1 | 12194 | 0.057 ms | 0.216 ms | 27.4 µs | 18675 B | 419 KB | 102.4 B | 0.000 B | 15958 KB | 0 |
| mix | 2 | 20487 | 0.054 ms | 0.314 ms | 27.0 µs | 18675 B | 424 KB | 102.4 B | 0.000 B | 15954 KB | 0 |
| mix | 4 | 28056 | 0.146 ms | 0.450 ms | 25.6 µs | 18675 B | 427 KB | 102.4 B | 0.000 B | 15954 KB | 0 |

Metrics cost: 125890 req/s with, 130333 req/s without (3.4%).

### jerryscript

| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | net/req (Server-Timing) | heap Δ/req after GC | min free heap | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| hello | 1 | 14423 | 0.068 ms | 0.097 ms | 18.2 µs | 1267 B | 542 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| hello | 2 | 28334 | 0.067 ms | 0.106 ms | 18.0 µs | 1267 B | 543 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| hello | 4 | 49291 | 0.074 ms | 0.143 ms | 15.3 µs | 1267 B | 546 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| jwks-hit | 1 | 3009 | 0.327 ms | 0.400 ms | 276.5 µs | 8138 B | 548 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| jwks-hit | 2 | 3632 | 0.545 ms | 0.641 ms | 266.6 µs | 8138 B | 550 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| jwks-hit | 4 | 3483 | 0.868 ms | 1.984 ms | 278.0 µs | 8138 B | 553 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| jwks-miss | 1 | 1030 | 0.941 ms | 1.686 ms | 899.1 µs | 14450 B | 551 KB | 2364.3 B | 0.000 B | 15809 KB | 0 |
| jwks-miss | 2 | 1012 | 0.940 ms | 1.800 ms | 909.6 µs | 14453 B | 554 KB | 2363.5 B | 0.000 B | 15809 KB | 0 |
| jwks-miss | 4 | 1012 | 0.959 ms | 1.760 ms | 911.3 µs | 14447 B | 558 KB | 2363.0 B | 0.000 B | 15809 KB | 0 |
| mix | 1 | 8712 | 0.055 ms | 0.398 ms | 61.7 µs | 1909 B | 548 KB | 112.0 B | 0.000 B | 15809 KB | 0 |
| mix | 2 | 12542 | 0.051 ms | 0.631 ms | 59.9 µs | 1909 B | 550 KB | 112.0 B | 0.000 B | 15809 KB | 0 |
| mix | 4 | 14784 | 0.307 ms | 0.916 ms | 59.3 µs | 1909 B | 553 KB | 112.4 B | 0.000 B | 15809 KB | 0 |

Metrics cost: 48898 req/s with, 48524 req/s without (-0.8%).

## A new connection for every request

`--close`: each request opens a connection and the server closes it after the response. `builtin` is `/__version`, which involves no JS, so it measures the event loop, the HTTP path and connection handling alone.

### quickjs

| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | net/req (Server-Timing) | heap Δ/req after GC | min free heap | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| builtin | 1 | 14727 | 0.066 ms | 0.107 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15992 KB | 0 |
| builtin | 2 | 23078 | 0.081 ms | 0.151 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15992 KB | 0 |
| builtin | 4 | 26330 | 0.145 ms | 0.278 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15992 KB | 0 |
| hello | 1 | 11830 | 0.080 ms | 0.147 ms | 7.2 µs | 4296 B | 367 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |
| hello | 2 | 17202 | 0.113 ms | 0.182 ms | 6.9 µs | 4296 B | 368 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |
| hello | 4 | 26732 | 0.137 ms | 0.330 ms | 6.3 µs | 4296 B | 370 KB | 4296.0 B | 0.000 B | 15988 KB | 0 |

### jerryscript

| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | net/req (Server-Timing) | heap Δ/req after GC | min free heap | errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| builtin | 1 | 14936 | 0.066 ms | 0.100 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15809 KB | 0 |
| builtin | 2 | 23602 | 0.080 ms | 0.147 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15809 KB | 0 |
| builtin | 4 | 28475 | 0.137 ms | 0.215 ms | 0.0 µs | 0 B | 0 KB | 0.0 B | 0.000 B | 15809 KB | 0 |
| hello | 1 | 11244 | 0.085 ms | 0.125 ms | 16.0 µs | 1302 B | 542 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| hello | 2 | 16522 | 0.116 ms | 0.173 ms | 17.0 µs | 1302 B | 543 KB | 280.0 B | 0.000 B | 15809 KB | 0 |
| hello | 4 | 31211 | 0.119 ms | 0.246 ms | 15.0 µs | 1302 B | 546 KB | 280.0 B | 0.000 B | 15809 KB | 0 |

## Startup

| engine | variant | boot to first request (inside / client) | boot phases µs (runtime, context, polyfills, module) | per-request phases µs (runtime, context, polyfills, module, call, teardown) | p50 latency |
|---|---|---|---|---|---:|
| quickjs | warm | 6.32 ms / 9.1 ms | 105 / 409 / 636 / 1950 | 0 / 0 / 0 / 0 / 10 / 0 | 197 µs |
| quickjs | cold, eager polyfills | 3.77 ms / 4.9 ms | 0 / 0 / 0 / 0 | 8 / 44 / 551 / 37 / 8 / 35 | 804 µs |
| quickjs | cold | 3.49 ms / 4.6 ms | 0 / 0 / 0 / 0 | 7 / 42 / 357 / 35 / 8 / 29 | 594 µs |
| quickjs | cold + bytecode | 4.44 ms / 5.4 ms | 0 / 0 / 0 / 0 | 6 / 39 / 48 / 30 / 6 / 27 | 274 µs |
| quickjs | warm + bytecode | 6.86 ms / 7.9 ms | 105 / 374 / 666 / 2107 | 0 / 0 / 0 / 0 / 8 / 0 | 124 µs |
| jerryscript | warm | 6.06 ms / 7.2 ms | 370 / 1 / 679 / 1363 | 0 / 0 / 0 / 0 / 21 / 0 | 139 µs |
| jerryscript | cold, eager polyfills | 4.68 ms / 6.2 ms | 0 / 0 / 0 / 0 | 2 / 0 / 707 / 30 / 16 / 42 | 915 µs |
| jerryscript | cold | 4.90 ms / 6.0 ms | 0 / 0 / 0 / 0 | 2 / 0 / 425 / 24 / 14 / 28 | 593 µs |


## How to read the tables

| Column | Source |
|---|---|
| req/s, p50, p99 | the client, end to end |
| CPU/req, alloc/req, peak heap, net/req | each response's `Server-Timing` header, averaged (peak: highest) |
| heap Δ/req after GC | `/__metrics?gc=1` before and after the cell, divided by its requests: memory the server keeps |
| min free heap | `/__metrics`: the lowest free heap of the 16 MB budget during the cell |

- **net/req** is taken when the response is written, so it includes garbage the next collection frees; **heap Δ/req after GC** is what stays.
- JerryScript's alloc and net figures are its arena's growth during the request plus ArrayBuffer storage; allocations freed within one stretch of work don't show.
- At 1 connection, throughput is bound by the client's round trip more than by the server.
- **Startup:** boot is measured from `main()` (inside) and from spawning the process (client), and includes compiling every module in the app. Warm engines pay the runtime, context, polyfill and module phases once at boot; cold engines pay them on every request.
- **Metrics cost** is `/` at 4 connections with `ISERE_METRICS=1` and `=0`, alternated twice.

To reproduce:

```sh
docker run --rm -v "$PWD":/app -w /app isere-linux node tools/bench.mjs \
  --build quickjs=build-linux-quickjs --build jerryscript=build-linux-jerryscript
docker run --rm -v "$PWD":/app -w /app isere-linux node tools/startup.mjs \
  --build quickjs=build-linux-quickjs --build jerryscript=build-linux-jerryscript
```
