# isère

![workflow](https://github.com/jeeyo/isere/actions/workflows/ci.yml/badge.svg)

isère is a small serverless runtime for microcontrollers. You write HTTP endpoints in JavaScript, in the Cloudflare Workers style, and isère serves them from a microcontroller board (first an NXP FRDM-K64F, then a Raspberry Pi Pico 2), on FreeRTOS, with QuickJS or JerryScript.

The aim is to show that a microcontroller can host a small, real-world service: verifying JWTs against a cached JWKS, publishing its own keys, and reporting live CPU and memory figures for every request. It is a personal project, not a general-purpose platform.

```js
// an endpoint: a module in app/, routed from app/routes.json
export default {
  async fetch(request, env) {
    return new Response('hello from isère\n')
  },
}
```

## Build your own app

An app is a directory of JavaScript modules and a `routes.json`. Your project includes isère the way projects include the Raspberry Pi Pico SDK:

```cmake
cmake_minimum_required(VERSION 3.19)
include(isere_import.cmake)    # copied from cmake/; finds isère and the board's toolchain
project(my_app C CXX ASM)
isere_init()
isere_add_app(my_app APP_DIRS app)
```

```sh
cmake -B build-k64f -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript && cmake --build build-k64f
```

Start from [`jeeyo/isere-hello-world`](https://github.com/jeeyo/isere-hello-world), a GitHub template whose copy lives in [`examples/hello-world`](examples/hello-world) here. isère's own firmware is built the same way, from `app/`.

## Current progress

Everything below works on the Linux build and is checked by CI (`tools/linux.sh`, both engines). On hardware, the NXP FRDM-K64F passes the same test suite with JerryScript (`tools/hil.sh`), and its benchmark numbers are in [BENCHMARK.md](BENCHMARK.md).

- [x] FreeRTOS kernel, with ports for Linux, the NXP FRDM-K64F (Cortex-M4F) and the Raspberry Pi Pico 2 (RP2350)
- [x] QuickJS (quickjs-ng) and JerryScript, both kept and tested
- [x] Several endpoints, routed by method and path, in warm engines
- [x] Endpoints isolated from each other: errors, timeouts, runaway loops and engine out-of-memory fail one request
- [x] HTTP/1.1 parsed in place, with keep-alive, pipelining and chunked bodies; fuzzed
- [x] Workers-style API: `Request`, `Response`, `Headers`, `URL`, `fetch` over HTTP and HTTPS, `crypto.subtle`, timers
- [x] JWT verification (RS256, ES256) against a cached JWKS, and the board's own `/.well-known/jwks.json`
- [x] Per-request metrics in `Server-Timing`, at `/__metrics` and as NDJSON, calibrated to within 10%
- [x] Black-box test suite, valgrind leak checks, fuzzer and benchmark, run in CI
- [x] Benchmarks of both engines on Linux: [BENCHMARK.md](BENCHMARK.md)

## Roadmap

- [x] Port isère to the NXP FRDM-K64F (Cortex-M4F, 256 KB SRAM, on-board Ethernet): FreeRTOS `ARM_CM4F`, lwIP over ENET, JerryScript
- [x] Run the hardware loop on the FRDM-K64F (`tools/hil.sh`): the full test suite passes there
- [x] Size the FRDM-K64F's stacks and heap from measurements (QuickJS does not fit its 256 KB)
- [ ] Fit TLS next to the engine on the FRDM-K64F
- [ ] HTTPS JWKS and OpenTelemetry export from the FRDM-K64F, straight over its Ethernet
- [x] Wall-clock time on the board (SNTP), so JWT expiry can be checked there
- [x] Benchmarks on the FRDM-K64F, published next to the Linux ones
- [ ] Later, the Raspberry Pi Pico 2: run its existing loop (`tools/hil.sh --board pico2`), route it to the outside world through the USB host, and publish its numbers
- [ ] Compare FreeRTOS/C with a Zephyr/Rust build on the same tests and benchmark, and choose one
- [ ] More boards: ESP32 Ethernet Kit ([#28](https://github.com/jeeyo/isere/pull/28))

The detailed plan and the definition of done are in [GOAL.md](GOAL.md).

## More

- [The isère story](https://jeeyo.github.io/isere/): how it got here, the numbers, and opinions (source in [`docs/`](docs/))
- [tools/README.md](tools/README.md): building, running, the test suite and the validation loops
- [BENCHMARK.md](BENCHMARK.md): benchmark results
- [PORTING.md](PORTING.md): porting isère to a new platform, including the FreeRTOS configuration

## Acknowledgment

Special thanks to

- [maxnet](https://github.com/maxnet/pico-webserver/) for the TinyUSB RNDIS to lwIP glue for the Raspberry Pi Pico
- [libuv](https://github.com/libuv/libuv) for [src/internals/uv_poll.c](src/internals/uv_poll.c)
- [picohttpparser](https://github.com/h2o/picohttpparser), whose stateless design the HTTP parser follows
- [librdkafka](https://github.com/confluentinc/librdkafka) for OpenTelemetry nanopb encoding
