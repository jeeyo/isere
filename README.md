# isère

![workflow](https://github.com/jeeyo/isere/actions/workflows/ci.yml/badge.svg)

isère is a small serverless runtime for microcontrollers. You write HTTP endpoints in JavaScript, in the Cloudflare Workers style, and isère serves them from a Raspberry Pi Pico 2, on FreeRTOS, with QuickJS or JerryScript.

The aim is to show that a microcontroller can host a small, real-world service: verifying JWTs against a cached JWKS, publishing its own keys, and reporting live CPU and memory figures for every request. It is a personal project, not a general-purpose platform.

```js
// an endpoint: a module in app/, routed from app/routes.json
export default {
  async fetch(request, env) {
    return new Response('hello from isère\n')
  },
}
```

## Current progress

Everything below works on the Linux build and is checked by CI (`tools/linux.sh`, both engines). None of it has been confirmed on the Pico 2 yet.

- [x] FreeRTOS kernel, with ports for Linux and the Raspberry Pi Pico 2 (RP2350)
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

- [ ] Run the hardware loop (`tools/hil.sh`) on a Pico 2 and confirm everything above there
- [ ] Size the Pico 2's stacks and heap from measurements, starting with QuickJS on the 24 KB httpd stack
- [ ] Route the Pico 2 to the outside world through the USB host, for HTTPS JWKS and OpenTelemetry export
- [ ] Wall-clock time on the board (SNTP), so JWT expiry can be checked there
- [ ] Benchmarks on the Pico 2, published next to the Linux ones
- [ ] Compare FreeRTOS/C with a Zephyr/Rust build on the same tests and benchmark, and choose one
- [ ] More boards: ESP32 Ethernet Kit ([#28](https://github.com/jeeyo/isere/pull/28))

The detailed plan and the definition of done are in [GOAL.md](GOAL.md).

## More

- [tools/README.md](tools/README.md): building, running, the test suite and the validation loops
- [BENCHMARK.md](BENCHMARK.md): benchmark results
- [PORTING.md](PORTING.md): porting isère to a new platform, including the FreeRTOS configuration

## Acknowledgment

Special thanks to

- [maxnet](https://github.com/maxnet/pico-webserver/) for the TinyUSB RNDIS to lwIP glue for the Raspberry Pi Pico
- [libuv](https://github.com/libuv/libuv) for [src/internals/uv_poll.c](src/internals/uv_poll.c)
- [picohttpparser](https://github.com/h2o/picohttpparser), whose stateless design the HTTP parser follows
- [librdkafka](https://github.com/confluentinc/librdkafka) for OpenTelemetry nanopb encoding
