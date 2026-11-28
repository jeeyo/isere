# Developing an isère app

This repository is an [isère](https://github.com/jeeyo/isere) app: HTTP endpoints written in JavaScript, embedded into firmware for a microcontroller board or into a Linux binary. isère is the runtime (FreeRTOS, a JS engine, an HTTP server); it lives in `isere/` as a git submodule. Change the app, not isère: an isère change belongs in isère's own repository, after which this repository updates the submodule.

## Layout

```
CMakeLists.txt        include(isere_import.cmake) → project() → isere_init() → isere_add_app(<target> APP_DIRS app)
isere_import.cmake    finds isère and the board's toolchain; keep it identical to isere/cmake/isere_import.cmake
app/routes.json       routes and config
app/*.js              modules: each file is one module, imported by file name
isere/                isère (submodule); its examples/jwks is a larger example (JWT verification)
```

`isere_add_app(<target> APP_DIRS dir1 dir2 ...)` embeds every `*.js` file and every static file of each directory into the firmware at build time, as C arrays in flash. Module names are file names, so they must be unique across directories; later directories' `config` wins. Nothing is read from a file system at run time.

## routes.json

```json
{
  "config": { "greeting": "hello" },
  "routes": [
    { "method": "GET", "path": "/", "module": "hello.js" },
    { "method": "POST", "path": "/echo", "module": "echo.js" },
    { "method": "*", "path": "/any", "module": "any.js" },
    { "method": "GET", "path": "/logo.svg", "static": "logo.svg", "type": "image/svg+xml" }
  ],
  "assets": [ "table.bin" ]
}
```

- **Paths match exactly.** There are no wildcards, prefixes or path parameters: `/a` doesn't match `/a/b`. Carry variable parts in the query string (`/item?id=42`). A path with no route answers 404, and a known path with the wrong method answers 405, without running JS. `HEAD` is served by `GET` routes.
- **Static routes** serve a file from flash with the given content type, without touching JS.
- **`assets`** lists data files (any bytes) to embed in flash for JS to read with `isere.asset()`; see [Data in flash](#data-in-flash-assets).
- **`config`** reaches every handler as `env`. On the Linux build, the `ISERE_CONFIG` environment variable (JSON) is merged over it.
- isère's own endpoints are always there: `GET /__version` (build ID, engine, warm/cold mode) and `GET /__metrics` (see [Measuring](#measuring)). Don't define these paths.

## Handlers

Two styles. The Workers style is preferred:

```js
// Workers style: a Request in, a Response (or a Promise of one) out
export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url)              // http://<Host header><path>?<query>
    const name = url.searchParams.get('name') || 'world'
    return new Response(`hello, ${name}\n`, { headers: { 'content-type': 'text/plain' } })
  },
}
```

```js
// Lambda style: event.httpMethod, path, query (raw string), headers (object), body
// (parsed JSON when it is JSON, else the raw string); context.env is the config
export const handler = async (event, context) => ({ statusCode: 200, headers: { 'content-type': 'application/json' }, body: { ok: true } })
```

- A handler may also return a plain string (200, `text/plain`). An object `body` in the Lambda style is sent as JSON.
- `Response.json(data, init)`, `request.text()`, `request.json()`, `request.arrayBuffer()` work. Bodies can be strings, `ArrayBuffer`s or typed arrays.
- Redirects are ordinary responses: `new Response(null, { status: 302, headers: { location: target } })`.
- A thrown error or a rejected promise answers **500**; a handler that hasn't answered after **5 s** gets **504**, and a busy loop is interrupted at that deadline.
- Modules import each other with relative names: `import { verify } from './jwt.js'`. Module-level state (caches, counters) persists across requests in the default warm mode; it is lost if the engine instance is replaced after an out-of-memory error.
- **Work ends with the response.** When a handler responds, isère cancels that request's pending timers and `fetch`es. `ctx.waitUntil(promise)` exists for compatibility but does not extend the request, so finish everything before responding.

## What exists in JS

isère's prelude provides a Cloudflare Workers-like subset. Only these globals exist beyond the language itself:

| Global | Notes |
|---|---|
| `Request`, `Response`, `Headers` | As above. Header names are lower-cased. |
| `fetch(url, init)` | HTTP and HTTPS, 10 s timeout, responses up to 128 KB, `Connection: close`. Rejects with `TypeError('fetch failed: ...')`. A fetch to `127.0.0.1` reaches the app's own server. HTTPS verifies certificates against a CA bundle read from a file (`ISERE_CA_FILE` on Linux); boards have no CA bundle yet, so **HTTPS fetches fail on a board** and only plain HTTP works there. |
| `URL`, `URLSearchParams` | A small implementation: `protocol host hostname port pathname search hash origin href searchParams`. `URL.canParse`. No user:password, no IPv6 literals. |
| `crypto.subtle.importKey` (JWK: RSASSA-PKCS1-v1_5, ECDSA P-256), `verify`, `digest` (SHA-256/384/512); `crypto.getRandomValues` | No signing, encryption or key generation. |
| `TextEncoder`, `TextDecoder` | UTF-8 only. |
| `atob`, `btoa` | |
| `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval`, `queueMicrotask`, `performance.now()` | Timers belong to the request that set them and are cancelled when it responds. |
| `console.log/info/warn/error` | Lines go to stderr on Linux and to the UART on a board. |
| `isere.asset(name)` | A file listed under `assets`, read from flash in slices; see below. |
| `isere.spin(ms)`, `isere.alloc(bytes)` | Calibration helpers; not for apps. |

Not available: Node.js or browser APIs (`require`, `process`, `Buffer`, `fs`, `window`, `document`, `localStorage`), `WebSocket`, `ReadableStream` bodies, `FormData`, `Blob`, `caches`, KV or any storage. Data an app needs must be in its source, config or assets, or be fetched.

## JavaScript: write for both engines

An app runs on QuickJS (quickjs-ng, near ES2023) or JerryScript (ES2020-ish), chosen at build time with `-DJS_RUNTIME=`. The FRDM-K64F board only fits **JerryScript**, so write for it:

- **No optional chaining (`?.`) or nullish coalescing (`??`)**: they are syntax errors in JerryScript. Use `x && x.y` and `x !== undefined && x !== null ? x : d`.
- Available in both: `let`/`const`, classes, arrow functions, `async`/`await`, Promises, template literals, destructuring, spread, ES modules, `Map`/`Set`, typed arrays, **`BigInt`**, `Array.prototype.at`, `flatMap`, `padStart`, `Object.fromEntries`, `encodeURI`/`decodeURI`.
- Check a construct on JerryScript before relying on it: build and run the app with `-DJS_RUNTIME=jerryscript` on Linux (below).

## Memory: the constraint that matters on a board

On the FRDM-K64F (Cortex-M4F at 120 MHz, 256 KB of SRAM):

| Resource | Limit |
|---|---|
| JerryScript heap (everything JS: compiled code, strings, objects) | **96 KB**, of which isère's prelude uses about 20 KB |
| C heap left for connections, `fetch`, TLS | about 85 KB |
| Request head / body | 8 KB / 16 KB (431 / 413 beyond) |
| Concurrent connections | 4 (more are queued or answered 503) |
| Flash for code + embedded app | 1 MB, about 600 KB used by isère with JerryScript |

- **Source text is cheap, objects are not.** The app's source sits in flash, but compiling it puts bytecode and every string literal into the JS heap, and every object, array element and property costs heap. Large tables written as object or array literals are the usual way to run out: a 3,600-entry dictionary of short strings took about 600 KB. Keep big data out of JS objects: put it in an asset and read only the slice a request needs, compute it, or fetch it.
- Every route's module stays resident once it has been loaded. `/__metrics` reports each route's resident size.
- Garbage collection runs whenever the JS heap grows by 1/32 of its size (3 KB on the board), so its cost spreads evenly over requests rather than pausing occasionally.
- When JerryScript runs out of heap, the request gets a 500 and isère replaces the engine instance (module state is lost). Out-of-C-memory answers 503.
- CPU: a minimal JS handler costs about 3 ms per request on the board; an RS256 signature verification about 90 ms.

## Data in flash (assets)

Large read-only data (dictionaries, lookup tables, fonts) belongs in an asset, not in JS. List the file under `"assets"` in `routes.json` (names must be unique across app directories; a missing file fails the build). It is embedded in flash as it is and costs no JS heap until read:

```js
const t = isere.asset('table.bin')   // null if no such asset
t.byteLength                         // size in bytes
t.read(offset, length)               // a new Uint8Array copy of that slice; read() copies it all
t.text(offset, length)               // the slice decoded as UTF-8
```

- Only the slice you read lands in the JS heap, so design the file for small reads: fixed-size records, an index at the start, sorted keys for binary search.
- Out-of-range reads throw a `RangeError`. Assets are read-only and are not served over HTTP (use a static route for that).
- Generate binary assets with a script in the app repository and commit the output, or generate them in `CMakeLists.txt` before `isere_add_app` runs.

## Building and running

```sh
# once: isère and only the submodules it needs (not --recursive: that pulls SDKs no board here needs)
git submodule update --init isere
git -C isere submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls 3rdparty/lwip

# Linux, for development (the default board)
cmake -B build -DJS_RUNTIME=jerryscript && cmake --build build -j
./build/<target> &                         # listens on :8080
curl -i http://127.0.0.1:8080/

# FRDM-K64F firmware (needs arm-none-eabi-gcc on PATH, in ARM_TOOLCHAIN_PATH, or in .toolchain/)
cmake -B build-k64f -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript && cmake --build build-k64f -j
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/kx.cfg \
  -c init -c "kinetis mdm halt" -c "program build-k64f/<target>.elf verify" -c "reset run" -c shutdown
```

- The Linux build runs on Linux only (in Docker on a Mac); the board build works on macOS and Linux.
- The board is chosen per build directory and can't change in one. The `<target>` name is the first argument of `isere_add_app` in `CMakeLists.txt`.
- On the board: DHCP over Ethernet, then SNTP; it prints `isere: ip A.B.C.D` and its log on the UART (115200 baud, OpenSDA's USB serial port). On macOS, read it with `stty` and `cat` on one handle: `(stty 115200 raw; cat) < /dev/cu.usbmodem*`, because macOS resets port settings on close.
- Useful Linux environment variables: `ISERE_CONFIG` (JSON over `config`), `ISERE_HANDLER_TIMEOUT_MS`, `ISERE_JS_MODE=cold` (a fresh engine per request), `ISERE_HEAP_SIZE` (budget the C heap like a board).

## Testing

- Test against a running server with plain HTTP: Node's built-in `node:test` and `fetch` need no dependencies. Start the Linux build, run the tests with the server's URL, stop it.
- Test on both engines on Linux, then on the board: a test suite that takes the base URL from an environment variable runs unchanged against `http://<board ip>:8080`.
- CI (`.github/workflows/build.yml`) builds and runs the app on Linux with both engines and builds the FRDM-K64F firmware. Add your tests there.

## Measuring

Every response has a `Server-Timing` header: `wall`, `cpu` (ms), `alloc`, `peak`, `net` (bytes) and `conn` (bytes the connection held). `GET /__metrics` (add `?gc=1` to collect garbage first) reports:

- `device`: `heap_size`, `heap_free`, `heap_min_free`, `heap_in_use`, `cpu_utilisation`, `httpd_stack_used`;
- `js`: `heap_used` (the JS heap) and `restarts` (engine instances replaced after running out of memory);
- `routes[]`: per route `count`, `errors`, `p50_us`, `p99_us`, `mean_cpu_us`, `mean_alloc` and `resident` (the module's JS heap).

Check `js.heap_used` and each route's `resident` after loading every route, and `heap_min_free` after a load test: on the board the JS heap must stay well under 96 KB.

## Updating isère

```sh
git -C isere fetch && git -C isere checkout <commit-or-tag>
git -C isere submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls 3rdparty/lwip
cmp isere_import.cmake isere/cmake/isere_import.cmake || cp isere/cmake/isere_import.cmake .
```

isère's `README.md`, `PORTING.md` and `BENCHMARK.md` (in `isere/`) have the details behind this file.
