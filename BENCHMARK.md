# Benchmark

Linux build, commit `281f738`, both JS engines. Pico 2 figures will be added from the same scripts through `tools/hil.sh`.

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

## Before the rewrite

Measured before the request path was rebuilt, with `wrk -t12 -c400` and a connection per request, on WSL2 on an Intel i5-9400F. Different hardware and method, so only the order of magnitude compares.

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
