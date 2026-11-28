# Porting isère to a new platform

This guide is for contributors bringing isère to a new board or operating system.

isère keeps everything platform-specific in one folder: `portable/<platform>/`. The rest is shared by every platform: the event loop, HTTP server, JS engines, `fetch`, crypto and metrics. Three ports exist and serve as worked examples:

| Port | What it is |
|---|---|
| [`portable/linux/`](portable/linux/) | A Linux process, using the FreeRTOS POSIX port. Used for development, tests and CI. |
| [`portable/frdm-k64f/`](portable/frdm-k64f/) | NXP FRDM-K64F (Cortex-M4F, 256 KB SRAM), using FreeRTOS's `ARM_CM4F` port, lwIP 2.2 over the on-board Ethernet, and NXP's SDK drivers vendored in `sdk/`. Validated on hardware with `tools/hil.sh`. |
| [`portable/pico2/`](portable/pico2/) | Raspberry Pi Pico 2 (RP2350), using FreeRTOS SMP and lwIP, networked over USB. Not yet run on hardware. |

You build a port with `cmake -DTARGET_PLATFORM=<platform>`. The value is the folder's name.

## What a port contains

```
portable/<platform>/
├── platform_features.cmake   before project(): toolchain, SDK, engine sizes and flags
├── platform_sources.cmake    the board's sources, appended to isère's core (FreeRTOS port, heap, drivers)
├── platform_extras.cmake     the board's flags and libraries on the core, and isere_platform_target()
├── include/
│   ├── FreeRTOSConfig.h      the kernel configuration (see below)
│   └── isere_config.h        isère's own sizes and limits
└── src/
    ├── platform.c            clocks, heap, stack watermark, board setup
    ├── tcp.c                 non-blocking TCP sockets
    ├── logger.c              log output
    └── rtc.c                 time of day
```

### How the build uses these files

isère is built through its CMake API (`cmake/isere.cmake`), for its own firmware and for app projects alike (see [`examples/hello-world`](examples/hello-world)). The board files plug into it:

1. **`platform_features.cmake`** is included by `cmake/isere_init.cmake` before the project's `project()`, at the top level. It chooses the toolchain and sets the variables below.
2. **`isere_init()`**, after `project()`, builds isère's core once as the target `isere_core`. It includes **`platform_sources.cmake`**, which appends the board's sources to `ISERE_CORE_SOURCES`, then **`platform_extras.cmake`**, which adds the board's include directories, definitions and libraries to `isere_core` (PUBLIC, so they reach every app; INTERFACE for an INTERFACE core).
3. **`isere_add_app(<target> APP_DIRS ...)`** creates each firmware image from `main.c`, the app's bundle and the core, then calls **`isere_platform_target(<target>)`** if `platform_extras.cmake` defines it: the place for per-image steps such as the linker script, map file, output suffix and image conversion (`portable/frdm-k64f/platform_extras.cmake` has an example).

Rules for board files:
- Refer to files through `ISERE_DIR` (isère's root) or `CMAKE_CURRENT_LIST_DIR`, never `CMAKE_SOURCE_DIR` or `CMAKE_CURRENT_SOURCE_DIR`: in an app project those are the app's directories. Inside `isere_platform_target()`, use `CMAKE_CURRENT_FUNCTION_LIST_DIR`.
- isère's core is an **OBJECT** library, so link-time overrides reach the link: a startup file's vector table, strong interrupt handlers over weak defaults, `syscalls.c` over the C library's stubs. A board whose SDK resolves its libraries per executable (pico-sdk) sets `ISERE_CORE_KIND` to `INTERFACE`, and isère's sources then compile in each app instead.
- isère creates these targets, so a board or app must not reuse the names: `isere_core`, `isere_quickjs`, `isere_jerry_internal`, `isere_mbedtls`, `jerry-core`, `jerry-ext`, `amalgam`, `lwipcore`, `lwipallapps`, `otelproto`.

### `platform_features.cmake`

| Variable | Meaning |
|---|---|
| `ISERE_JS_STACK_KB` | C stack a JS engine may use, counted from where each stretch of JS work starts. It must be well below the httpd task's stack (see [Stacks](#stacks)). Deep recursion beyond it raises a `RangeError` instead of overflowing. |
| `JERRY_GLOBAL_HEAP_SIZE` | JerryScript's arena, in KB. Each engine instance gets one: one in warm mode, one per request in cold mode. |
| `ISERE_CORE_KIND` | `OBJECT` (default) or `INTERFACE`, see above. |
| `ISERE_QUICKJS_DEFINITIONS`, `ISERE_QUICKJS_OPTIONS` | Compile definitions and options for QuickJS's sources and its binding on this board (see [What each engine needs from a port](#what-each-engine-needs-from-a-port)). |
| `WITH_LWIP` | Builds isère's own lwIP copy (`3rdparty/lwip`, 2.2.1, whose `contrib/ports/freertos` provides `sys_arch`). The FRDM-K64F uses it; the Pico 2 uses pico-sdk's instead. |

### `isere_config.h`

| Setting | Linux | FRDM-K64F | Pico 2 | Meaning |
|---|---:|---:|---:|---|
| `ISERE_TCP_MAX_CONNECTIONS` | 12 | 4 | 4 | Connections from clients. Two more are reserved for connections the server opens to itself (`fetch` to its own endpoints). |
| `ISERE_HTTPD_TASK_STACK_SIZE` | `configMINIMAL_STACK_SIZE` | 5120 words | 6144 words | The task that runs everything isère does. |

Further sizes have defaults in the headers and can be overridden here:
- the per-connection head and body budgets: `ISERE_HTTPD_HEAD_BUDGET` and `ISERE_HTTPD_BODY_BUDGET` in `include/httpd.h`
- the largest `fetch` response: `ISERE_FETCH_MAX_RESPONSE` in `src/fetch.c`
- the handler deadline: `ISERE_HANDLER_TIMEOUT_MS` in `src/engine.c`

## Choosing a JavaScript engine

isère runs the same app on either of two engines, chosen when the build is configured:

```sh
cmake -B build -DTARGET_PLATFORM=<platform> -DJS_RUNTIME=quickjs       # the default; ISERE_BOARD also works
cmake -B build -DTARGET_PLATFORM=<platform> -DJS_RUNTIME=jerryscript
```

`JS_RUNTIME` is a top-level option (`CMakeLists.txt`), not something a port sets: one port can build either engine, and a build directory holds one. The scripts take it as `--runtime` (`tools/linux.sh`, `tools/hil.sh`). Both engines go through the same binding interface (`src/runtimes/<engine>/vm.c`), run the same prelude and pass the same test suite; CI builds and tests both on every push.

Independently of the engine, `ISERE_JS_MODE` selects **warm** (the default: one engine instance for the whole process) or **cold** (a fresh instance per request). It is read with `getenv()` at start-up; on a board, compile it in with `ISERE_BOARD_ENV` (for example `-DISERE_BOARD_ENV="ISERE_JS_MODE=cold"`, see [Pitfalls](#pitfalls-the-frdm-k64f-port-found)). Cold mode needs room for an instance per request in flight, so on a small board it multiplies the memory below by the number of connections.

### Which engine fits a board

| | QuickJS (quickjs-ng) | JerryScript |
|---|---|---|
| Memory | Allocates from isère's heap as it goes, with no fixed ceiling. Peak heap for a hello at one connection on 64-bit Linux: 367 KB. | A fixed arena per instance (`JERRY_GLOBAL_HEAP_SIZE`), allocated once as **one contiguous block**, plus small allocations outside it. |
| Speed (64-bit Linux, keep-alive, `BENCHMARK.md`) | 1.1× JerryScript's throughput for a hello at one connection, 2.0× at four; 1.75–2.2× for an RS256 verification. | Slower, but bounded: the arena caps what a handler can use. |
| C stack (64-bit Linux, after boot) | 21.1 KB | 5.9 KB |
| FRDM-K64F (256 KB SRAM, 217 KB heap) | Does not fit: ran out of memory parsing the prelude, before serving a request. | Fits with a 96 KB arena: all tests pass, about 85 KB of heap stays free. |

Start a new board with **JerryScript** if it has under about 512 KB of RAM, and confirm QuickJS from measurements if you want its speed. Then size the engine:

- **JerryScript's arena** must hold the prelude (about 20 KB) and every route's module (each stays resident in a warm instance), plus what one request needs at its peak: a 16 KB request body with its decoded text and an echoed JSON copy needed about 48 KB. On the FRDM-K64F, 48 KB ran out on many headers, 80 KB ran out once the test routes' modules were resident, and 96 KB passes. Read `js.heap_used` and the per-route `resident` figures at `/__metrics` after running the suite.
- **Leave the rest for the C heap.** The arena competes with connection buffers, `/__metrics` and a TLS handshake (40–60 KB). On the FRDM-K64F, a 96 KB arena with nothing else trimmed left too little; it fit after measured trims elsewhere (smaller network buffers, an httpd stack sized from its high-water mark).
- **The arena needs one contiguous block.** With `heap_5` over several RAM banks, it must fit in a single region: the K64F's 64 KB SRAM_L can never hold it.
- **`ISERE_JS_STACK_KB`** is the engine's C stack budget for both engines (JerryScript's `JERRY_STACK_LIMIT` is set from it). On the FRDM-K64F, unbounded recursion stopped at a 16 KB budget peaked at 17.5 KB of httpd stack, and at 15.1 KB with a 14 KB budget; size the httpd task from `device.httpd_stack_used` after the recursion test.

### What each engine needs from a port

**QuickJS** assumes a POSIX host. On a newlib board, set these through `ISERE_QUICKJS_DEFINITIONS` and `ISERE_QUICKJS_OPTIONS` in `platform_features.cmake` (see `portable/frdm-k64f/` and `portable/pico2/`):
- compile its sources with `EMSCRIPTEN` (no pthreads or C11 atomics, which newlib lacks; this also turns off computed-goto dispatch) and `_POSIX_MONOTONIC_CLOCK=200112L`, and force-include a small compat header (`include/quickjs_compat.h`) for the `PRId64`-style macros some toolchains hide;
- provide `clock_gettime()`, which newlib declares only with `_POSIX_TIMERS`: the K64F port implements it on its PIT timer in `src/syscalls.c` and declares it in the compat header;
- with GCC 14 on arm-none-eabi, add `-Wno-incompatible-pointer-types` for its sources: `int32_t` is `long` there, and quickjs-ng mixes `int *` and `int32_t *`, which GCC 14 makes an error;
- link `libm`.

**JerryScript** needs less:
- `JERRY_GLOBAL_HEAP_SIZE` in `platform_features.cmake`, as a cache string in KB, for example `set(JERRY_GLOBAL_HEAP_SIZE "(96)" CACHE STRING "")`;
- the port's include directories on the `jerry-core` target, if its headers need board definitions;
- nothing for CMake 4: JerryScript's own `CMakeLists.txt` declares a minimum older than CMake 4 accepts, and isère sets `CMAKE_POLICY_VERSION_MINIMUM=3.5` for it automatically.

isère allocates JerryScript's context and arena itself before `jerry_init()` (`src/runtimes/jerryscript/vm.c`), so an arena that doesn't fit makes the engine fail to start with an error instead of crashing.

### Checking the choice on hardware

`/__version` reports the running engine (`"runtime"`) and mode (`"js_mode"`), and the boot log prints both: `[httpd] Listening on port 8080 (jerryscript, warm)`. An engine that can't start logs `unable to start the JS engine` and httpd does not listen. Then run the suite and the benchmark through `tools/hil.sh --runtime <engine>` and compare `heap_min_free`, `js.heap_used` and `httpd_stack_used` at `/__metrics` against the sizes above.

## Modules to implement

Every isère module runs on **one task**, the httpd task, around one event loop. Your platform code never has to be reentrant with respect to isère, but it must never block for long: a blocking call stalls every connection.

### `platform.c`: [`portable/include/platform.h`](portable/include/platform.h)

| Function | Contract |
|---|---|
| `platform_init()` | Board setup before anything else, such as clocks, UART and LEDs. |
| `platform_exit(code)` | Ends the program after a clean shutdown (Linux: `exit`). Firmware that never exits returns, and the httpd task deletes itself. |
| `platform_now_us()`, `platform_now_ns()` | A monotonic clock. Timers, deadlines and latency metrics use it. |
| `platform_time_ms()` | Wall-clock time since the Unix epoch. JWT expiry checks depend on it. |
| `platform_task_cpu_ns()` | CPU time of the calling task, if the platform has it. By default, per-request CPU time uses `platform_now_ns()`: a stretch of request work never blocks, so the time it holds the task is its CPU time. On Linux, `ISERE_CPU_CLOCK=thread` switches to this function. |
| `platform_busy_cpu_ns()` | CPU time of everything except idle, for the device CPU-utilisation metric. On FreeRTOS: total time minus the idle tasks' run-time counters. |
| `platform_malloc()`, `platform_free()` | The heap underneath isère's accounting allocator. Use the C library's `malloc` if it's thread-safe, otherwise `pvPortMalloc`. Avoid heap_3's critical sections where they are expensive: on the POSIX port they cost two syscalls per call, which halved QuickJS's throughput. |
| `platform_heap_size()` | The heap budget isère accounts against. "Free heap" and out-of-memory behaviour derive from it. |
| `platform_stack_watch()`, `platform_stack_used()` | The httpd task's stack high-water mark, reported at `/__metrics`. On FreeRTOS hardware it comes from `uxTaskGetStackHighWaterMark`. |

### `tcp.c`: [`include/tcp.h`](include/tcp.h)

Sockets are non-blocking. A call that would block returns `ISERE_TCP_AGAIN`, and the event loop waits for readiness with `isere_tcp_poll()`.

| Function | Contract |
|---|---|
| `isere_tcp_socket_new()` | Returns a non-blocking socket. |
| `isere_tcp_accept(fd, ip, &port)` | Returns a non-blocking socket, or `ISERE_TCP_AGAIN`. The peer's address and port are how httpd recognises connections the server opened to itself. |
| `isere_tcp_connect()` | Returns 0, `ISERE_TCP_AGAIN` while connecting (completion is reported through `POLLOUT` and `isere_tcp_socket_last_error()`), or -1. |
| `isere_tcp_recv()`, `isere_tcp_write()` | Byte counts, `ISERE_TCP_AGAIN`, 0 on close (recv), or -1. They must not raise `SIGPIPE`. |
| `isere_tcp_local_port()` | The local port of a socket, for outbound connections. |
| `isere_tcp_resolve()` | Host name to dotted IPv4. It may block briefly; `fetch` calls it once per request. |
| `isere_tcp_poll()` | `poll()` semantics, in milliseconds. The event loop computes the timeout from its timers, so the platform's timeout resolution is the timers' resolution. |
| `isere_tcp_is_initialized()` | Whether the network is up. httpd waits for it before listening. |

### `logger.c`, `rtc.c`

- **logger**: `error`, `warning`, `info` and `debug` with a tag and a format. Serialise output if other tasks also print.
- **rtc**: get and set the date and time.

There is no file system: the app, its static files and its assets are embedded in flash at build time.

### Pitfalls the FRDM-K64F port found

The first port to run on hardware found these. Each is now fixed in shared code or easy to get right in a new port:

- **`main()`'s locals don't survive the scheduler.** On Cortex-M, `vTaskStartScheduler()` resets the main stack pointer and interrupts reuse `main()`'s frame. Anything a task keeps a pointer to must be static (`src/main.c` does this now).
- **`poll()` flags differ between socket layers.** `POLLOUT` is `0x4` on Linux but `0x2` in lwIP, where `0x4` is `POLLERR`. isère's `UV_POLLIN`/`UV_POLLOUT` now come from the socket layer's own headers; a hard-coded value makes every wait for writability hang.
- **`fetch` to `127.0.0.1` needs a loopback interface.** In lwIP that is `LWIP_HAVE_LOOPIF` and `LWIP_NETIF_LOOPBACK`; the reference verifier fetches its JWKS from `127.0.0.1`.
- **newlib-nano's `printf` has no `%zu` or 64-bit integers**, which `/__metrics` uses. Link full newlib (drop `--specs=nano.specs`), and give every task that prints a few KB more stack for it.
- **Size the heap for its largest block, not its total.** On a small heap, a 16 KB request buffer growing to 20 KB needs both at once. httpd now sizes buffers exactly and backs off when memory is fragmented, and answers 503 rather than a truncated 200 or a false 413 when it still can't.
- **A board can't be given environment variables.** Settings the tests expect (such as the 1 s handler deadline in `tests/e2e/lib/target.mjs`) are compiled in with `ISERE_BOARD_ENV` and applied with `setenv()` at boot.
- **Set the wall clock.** JWT expiry needs real time; the K64F port starts SNTP after DHCP and waits up to 10 s for it before serving.

### Randomness and TLS

- **Randomness:** mbedTLS needs an entropy source (`include/isere_mbedtls_config.h`). On Linux it reads `/dev/urandom`. On a board, define `MBEDTLS_ENTROPY_HARDWARE_ALT` and implement `mbedtls_hardware_poll()`, for example from the RP2350's TRNG.
- **HTTPS:** `fetch` needs CA certificates. Linux reads a file (`ISERE_CA_FILE`); a board needs them embedded.

## FreeRTOS configuration

`FreeRTOSConfig.h` holds only settings that differ from FreeRTOS's defaults (`3rdparty/FreeRTOS/include/FreeRTOS.h`), each with a comment saying why. Keep it that way in a new port: a setting that equals the default, or that nothing uses, doesn't belong in the file.

### What isère needs from the kernel

Very little.
- **Tasks:** the httpd task, created with `xTaskCreate`. It waits for the network with `vTaskDelay`. At shutdown it frees everything, then calls `platform_exit()`; it doesn't unwind the scheduler, because `vTaskEndScheduler` in the POSIX port can hang under valgrind on x86-64.
- **A mutex** in the logger.
- **The heap**, if `platform_malloc` uses `pvPortMalloc`.

Software timers are **not** used: JS timers live on the event loop. The Pico 2 port has more users of the kernel: TinyUSB, lwIP's `tcpip` thread with its FreeRTOS `sys_arch`, and pico-sdk's interop.

### Essential settings

Without these, isère doesn't build or doesn't work.

| Setting | Value | Why |
|---|---|---|
| `configUSE_PREEMPTION` | 1 | Required by FreeRTOS. With preemption off, Linux lost 5% throughput, and a request waiting on a 1 ms JS timer took 2.8 ms instead of 1.8 ms. On a board, other tasks (USB, lwIP) must run while JS is busy. |
| `configUSE_IDLE_HOOK`, `configUSE_TICK_HOOK` | 0 | Required by FreeRTOS. isère has no hooks. |
| `configMAX_PRIORITIES` | ≥ 4 | The httpd task runs at `tskIDLE_PRIORITY + 2`. Your network stack may need more: lwIP's `tcpip` thread runs at `+4`, so the Pico 2 uses 7. |
| `configMINIMAL_STACK_SIZE` | per port | Required. It sizes the idle tasks. |
| `configTICK_TYPE_WIDTH_IN_BITS` | `TICK_TYPE_WIDTH_32_BITS` | FreeRTOS **defaults to 16-bit ticks**, which wrap after 65 s at 1 kHz. |
| `configUSE_MUTEXES` | 1 | The logger's mutex. |
| `INCLUDE_vTaskDelay`, `INCLUDE_vTaskDelete` | 1 | The httpd task uses both. |
| `configASSERT(x)` | `assert(x)` | Kernel misuse fails loudly. It costs nothing measurable. |
| `configGENERATE_RUN_TIME_STATS`, `INCLUDE_xTaskGetIdleTaskHandle` | 1 | Only if `platform_busy_cpu_ns()` uses the idle tasks' run-time counters, as on the Pico 2. Use a 64-bit `configRUN_TIME_COUNTER_TYPE` with a microsecond counter, so it doesn't wrap after 71 minutes. |

Your RTOS port and libraries add their own essentials. On the RP2350:
- **the port:** `configENABLE_FPU` (both JS engines use floating point), `configENABLE_MPU`, `configENABLE_TRUSTZONE`, `configRUN_FREERTOS_SECURE_ONLY`, `configMAX_SYSCALL_INTERRUPT_PRIORITY`
- **SMP:** `configNUMBER_OF_CORES`, `configTICK_CORE`, `configRUN_MULTIPLE_PRIORITIES`, `configUSE_CORE_AFFINITY`, `configUSE_PASSIVE_IDLE_HOOK`, `portSUPPORT_SMP`
- **pico-sdk:** `configSUPPORT_PICO_SYNC_INTEROP` and `configSUPPORT_PICO_TIME_INTEROP`
- **lwIP's `sys_arch`:** recursive mutexes, counting semaphores, `INCLUDE_vTaskSuspend`

Find yours by grepping the port and the libraries for `config` and `INCLUDE_`.

### Effective settings: measure them

These change performance or memory. Choose each value from a measurement taken on the target, not from another port.

**Tick rate.** On Linux the tick is a signal that interrupts the httpd task. JS timers wait in `poll()`, which takes milliseconds, so the tick costs only idle CPU there:

| Linux, QuickJS, `wrk -c4` on `/` | Idle CPU | Throughput, p99 | Request waiting on a 1 ms JS timer |
|---|---:|---|---:|
| 1000 Hz | 4.1% | 94.1k req/s, 81 µs | 1.78 ms |
| **100 Hz** (chosen) | **1.5%** | 94.4k req/s, 79 µs | 1.83 ms |
| 10000 Hz | 12.0% | 90.8k req/s, 84 µs | 1.71 ms |

If your network stack counts timeouts in ticks, as lwIP does on the Pico 2, the tick rate becomes the resolution of every JS timer and poll timeout. Keep 1000 Hz there. [`tools/freertos_measure.sh`](tools/freertos_measure.sh) produces this table for the Linux build.

**Stacks.** The httpd task's stack must hold the JS stack budget (`ISERE_JS_STACK_KB`) plus what isère itself uses: about 7 KB on 64-bit Linux, more while mbedTLS performs a handshake. Measure it from `device.httpd_stack_used` at `/__metrics` after exercising the suite; on FreeRTOS hardware that needs `INCLUDE_uxTaskGetStackHighWaterMark`. On 64-bit Linux:

| After | QuickJS | JerryScript |
|---|---:|---:|
| boot (prelude and every module compiled) | 21.1 KB | 5.9 KB |
| RS256 verification with the JWKS fetched over HTTPS | 36.4 KB | 9.7 KB |
| unbounded recursion, stopped at the 64 KB JS budget | 71.1 KB | 66.4 KB |

A 32-bit board needs less, but likely not half as much. The Pico 2's 24 KB httpd stack with a 16 KB budget is tight for QuickJS and is the first thing to check there. While sizing stacks, `configCHECK_FOR_STACK_OVERFLOW 2` (with a `vApplicationStackOverflowHook`) catches overflows on hardware.

### How the two existing ports differ, and why

| Difference | Reason |
|---|---|
| Tick rate: 100 Hz on Linux, 1000 Hz on the Pico 2 | Linux's `poll()` takes milliseconds; lwIP counts its timeouts in ticks. |
| `configMAX_PRIORITIES`: 4 vs 7 | lwIP's `tcpip` thread at `+4` exists only on the Pico 2. |
| Port and SMP settings | Only the RP2350 port has them. |
| Recursive mutexes, counting semaphores, `vTaskSuspend`, scheduler state | Used by lwIP, TinyUSB and pico-sdk, which only the Pico 2 links. |
| Run-time stats | Linux reads the process CPU clock instead. |
| `configMINIMAL_STACK_SIZE`, `configSTACK_DEPTH_TYPE` | In the POSIX port the pthread stack is what counts. glibc's minimum is 128 KB on aarch64, so the value only sizes the idle task. |

## Validating a port

The black-box suite in `tests/e2e/` runs against any running server:

```sh
ISERE_URL=http://<board-ip>:8080 node --test --test-concurrency=1 'tests/e2e/*.test.mjs'
node tools/bench.mjs --url http://<board-ip>:8080
```

- Build with `-DISERE_APP_DIRS="<repo>/examples/jwks/app;<repo>/tests/e2e/app"`, so the test endpoints are there.
- Tests that need the Linux build skip themselves; these use environment variables, valgrind or the server's output.
- [`tools/hil.sh`](tools/hil.sh) wraps build, flash, boot check, the suite and the benchmark for the FRDM-K64F (`--board frdm-k64f`, the default) and the Pico 2 (`--board pico2`, below). Add a case to it for a new board.
- After a run, `/__metrics` shows `device.heap_min_free` and `device.httpd_stack_used`. They are the evidence that the heap and stack sizes fit.

### The Pico 2 hardware loop (not yet validated)

The Pico 2 port builds, but has not yet run the suite on hardware. `tools/hil.sh --board pico2` (QuickJS by default; `--runtime jerryscript` for the other engine) is ready for it.


One iteration on the Pico 2 (`--board pico2`, QuickJS by default):
1. builds the firmware with a unique build ID
2. flashes it over SWD with a Raspberry Pi Debug Probe (no BOOTSEL button, works even if the previous build crashed)
3. records the board's UART output through the probe
4. waits for the board's USB network interface (MAC `02:02:84:6a:96:00`) to get an IPv4 address
5. waits until `http://192.168.7.1:8080/__version` reports the build ID from step 1, so it can't test stale firmware
6. runs the test suite against the board
7. benchmarks with `wrk` at 1, 2 and 4 connections, then checks the board still answers

#### Wiring

```
Mac ──USB── Debug Probe ──"D" (SWD) cable── Pico 2 SWD header (SWCLK / GND / SWDIO)
                        └─"U" (UART) cable── Pico 2 GP0 (UART0 TX) → probe RX
                                             Pico 2 GP1 (UART0 RX) ← probe TX
                                             GND ── GND
Mac ──USB── Pico 2 (power + CDC-ECM network, 192.168.7.1)
```

#### Setup on macOS

```zsh
brew install cmake node wrk
```

You also need:
- **ARM toolchain.** Either `brew install --cask gcc-arm-embedded`, or the toolchain the Raspberry Pi Pico VS Code extension installs under `~/.pico-sdk/toolchain/`. If you use the extension's toolchain, set `PICO_TOOLCHAIN_PATH` to it.
- **openocd with RP2350 support.** That means the Raspberry Pi fork, which the Pico VS Code extension installs under `~/.pico-sdk/openocd/<version>/openocd`. `hil.sh` finds it there automatically. Any other build works if it has `target/rp2350.cfg`; point `OPENOCD` at it. The Homebrew `open-ocd` release may not have RP2350 support yet.
- **Submodules.** Run `git submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/llhttp 3rdparty/libyuarel 3rdparty/pico-sdk`, then `git -C 3rdparty/pico-sdk submodule update --init lib/tinyusb lib/lwip`.

On Apple silicon Macs, macOS may ask "Allow accessory to connect?" the first time each USB device appears. Allow both the probe and the Pico 2.

If more than one `/dev/cu.usbmodem*` device exists, set `HIL_SERIAL` to the probe's UART.
