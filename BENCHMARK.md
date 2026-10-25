# Benchmark

All figures below are from commit `281f738` on the Linux build, both engines, measured with [`tools/bench.mjs`](tools/bench.mjs) and [`tools/startup.mjs`](tools/startup.mjs) (criteria 7, 8 and 10). Pico 2 figures will come from the same script through `tools/hil.sh` and aren't measured yet.

## Setup

- **Host:** Apple-silicon Mac, Docker (colima VM, 6 vCPUs, 8 GB) with Ubuntu 24.04 aarch64 (`tools/linux.Dockerfile`). Four unrelated, mostly idle service containers ran in the same VM.
- **Build:** `RelWithDebInfo`, warm engines (the default), `ISERE_NDJSON=0`, everything else at its defaults: 16 MB heap budget, 12-connection pool, 100 Hz FreeRTOS tick.
- **Client:** `tools/bench.mjs` in the same container. Every connection is keep-alive with one request in flight. Latency is measured at the client; CPU, allocation, peak and net heap come from each response's `Server-Timing` header; the heap change after GC and the lowest free heap come from `/__metrics?gc=1` before and after each cell.
- **Runs:** each engine ran 12 cells of 50 s (four scenarios at 1, 2 and 4 connections, 4 being the Pico 2's connection limit), about 10 minutes per engine, then the metrics-overhead check. Each cell starts a fresh server.

| scenario | what each request does |
|---|---|
| hello | `GET /`: the smallest JS handler |
| jwks-hit | `GET /verify` with an RS256 token; the JWKS is in the cache |
| jwks-miss | the same with `jwks_ttl_ms: 0`, so every verification fetches the JWKS from this same server over loopback first |
| mix | in turn: `/`, `/verify`, the static `/.well-known/jwks.json`, `/__version`, a 404 |

## Reliability

| | QuickJS | JerryScript |
|---|---:|---:|
| requests over the 10-minute run | 12,336,255 | 7,063,209 |
| errors (wrong status, dropped connection, socket error) | 0 | 0 |
| crashes or engine restarts | 0 | 0 |
| heap change per request after GC, worst cell | -0.018 B | 0.000 B |

valgrind on both engines, warm and cold, shows no invalid accesses and no growth in lost or retained memory between a 20- and a 120-request run (`tests/e2e/leak.test.mjs`).

## Results (keep-alive)

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

Before commit `281f738`, closed connections left dead slots in the poll set, so `builtin` at 4 connections managed 9.3k req/s and got slower with every connection served. It now runs at 26-28k req/s, on par with a bare Node.js server measured the same way.

## Startup (criterion 10)

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

- **Boot to first request** is measured from `main()` (inside) and from spawning the process (client). It includes compiling the prelude and every module in the app, test endpoints included.
- **Warm** engines pay the runtime, context, polyfill and module phases once at boot; a request costs only the call.
- **Cold** engines pay them on every request. Parsing the prelude dominates; lazy prelude parts cut it by about 35-40%, and QuickJS bytecode almost removes it.

## Reading the numbers

- **QuickJS is faster than JerryScript** on every scenario now: twice the throughput on `/` (98k against 49k req/s at 4 connections) and on JWT verification (7.6k against 3.5k). Before the rewrite it was the other way round (300 against 3,720 req/s under `wrk`), because QuickJS built a whole runtime for each request.
- **A JWKS cache miss** costs one loopback fetch plus a fresh key import. On QuickJS that is 158 µs against 122 µs for a hit; on JerryScript 900 µs against 270 µs, mostly in parsing the JWKS JSON and importing the key in JavaScript.
- **QuickJS's `net/req` from Server-Timing is 4.3 KB on `/`**, while the heap change after GC is zero. The request's objects are garbage at the moment the header is written and are freed by the next collection. JerryScript's arena behaves similarly, at a smaller scale.
- **JerryScript's allocation figures** count its arena's growth during the request plus ArrayBuffer storage, which goes through isère's allocator; allocations freed within the same stretch are invisible (see `GOAL.md`, per-request CPU accounting).
- **Throughput at 1 connection is bound by the round trip**, not the server: at 4 connections the same server serves several times more.
- **Metrics cost** less than 5% of throughput (3.4% on QuickJS, within noise on JerryScript), measured as `/` at 4 connections with `ISERE_METRICS=1` and `=0`, alternated twice.

## Before the rewrite

These are the figures this file held before the request path was rebuilt. They were measured with `wrk -t12 -c400` and a connection per request, on different hardware (WSL2 on an i5-9400F), so they only show the order of magnitude.

Tested with WSL2 on Intel(R) Core(TM) i5-9400F CPU @ 2.90GHz with 16GB RAM

### Node 20

```bash
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    42.32ms  149.54ms   1.99s    96.70%
    Req/Sec     1.71k   648.15     7.07k    85.64%
  190318 requests in 10.07s, 23.78MB read
  Socket errors: connect 0, read 0, write 0, timeout 134
Requests/sec:  18902.95
Transfer/sec:      2.36MB
```

### isère

#### QuickJS

```bash
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    57.24ms  186.63ms   1.81s    95.36%
    Req/Sec    64.04     52.16   310.00     77.97%
  3027 requests in 10.06s, 103.46KB read
  Socket errors: connect 0, read 3027, write 0, timeout 9
Requests/sec:    300.86
Transfer/sec:     10.28KB
```

#### JerryScript

```bash
$ wrk -t12 -c400 -d10s http://127.0.0.1:8080
Running 10s test @ http://127.0.0.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    30.59ms  159.42ms   1.77s    95.66%
    Req/Sec   514.20    453.09     3.20k    67.48%
  37427 requests in 10.06s, 1.93MB read
  Socket errors: connect 0, read 37427, write 0, timeout 14
Requests/sec:   3720.05
Transfer/sec:    196.19KB
```

### isère on Raspberry Pi Pico 2

#### QuickJS

```bash
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   218.71ms  128.51ms 817.69ms   82.91%
    Req/Sec     4.57      4.25    20.00     53.98%
  117 requests in 10.05s, 4.03KB read
  Socket errors: connect 0, read 4071, write 0, timeout 0
Requests/sec:     11.64
Transfer/sec:     410.87B
```

#### JerryScript

```bash
$ wrk -t12 -c400 -d10s http://192.168.7.1:8080
Running 10s test @ http://192.168.7.1:8080
  12 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    62.76ms  159.71ms   1.00s    96.57%
    Req/Sec    21.60     16.20    70.00     58.79%
  408 requests in 10.06s, 21.52KB read
  Socket errors: connect 0, read 4789, write 0, timeout 0
Requests/sec:     40.56
Transfer/sec:      2.14KB
```
