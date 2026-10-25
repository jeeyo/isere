# Porting isère to a new platform

This guide is for contributors bringing isère to a new board or operating system.

isère keeps everything platform-specific in one folder: `portable/<platform>/`. The rest is shared by every platform: the event loop, HTTP server, JS engines, `fetch`, crypto and metrics. Two ports exist and serve as worked examples:

| Port | What it is |
|---|---|
| [`portable/linux/`](portable/linux/) | A Linux process, using the FreeRTOS POSIX port. Used for development, tests and CI. |
| [`portable/pico2/`](portable/pico2/) | Raspberry Pi Pico 2 (RP2350), using FreeRTOS SMP and lwIP, networked over USB. |

You build a port with `cmake -DTARGET_PLATFORM=<platform>`. The value is the folder's name.

## What a port contains

```
portable/<platform>/
├── platform_features.cmake   options set before project(): toolchain, SDK, engine sizes
├── platform_sources.cmake    platform sources added to the build (FreeRTOS port, heap, drivers)
├── platform_extras.cmake     include paths, libraries and flags added after the target exists
├── include/
│   ├── FreeRTOSConfig.h      the kernel configuration (see below)
│   └── isere_config.h        isère's own sizes and limits
└── src/
    ├── platform.c            clocks, heap, stack watermark, board setup
    ├── tcp.c                 non-blocking TCP sockets
    ├── logger.c              log output
    ├── rtc.c                 time of day
    └── fs.c                  reserved; may be empty
```

### `platform_features.cmake`

| Variable | Meaning |
|---|---|
| `ISERE_JS_STACK_KB` | C stack a JS engine may use, counted from where each stretch of JS work starts. It must be well below the httpd task's stack (see [Stacks](#stacks)). Deep recursion beyond it raises a `RangeError` instead of overflowing. |
| `JERRY_GLOBAL_HEAP_SIZE` | JerryScript's arena, in KB. Each engine instance gets one: one in warm mode, one per request in cold mode. |
| `WITH_LWIP` | Builds isère's own lwIP copy. The Pico 2 uses pico-sdk's instead. |

### `isere_config.h`

| Setting | Linux | Pico 2 | Meaning |
|---|---:|---:|---|
| `ISERE_TCP_MAX_CONNECTIONS` | 12 | 4 | Connections from clients. Two more are reserved for connections the server opens to itself (`fetch` to its own endpoints). |
| `ISERE_HTTPD_TASK_STACK_SIZE` | `configMINIMAL_STACK_SIZE` | 6144 words | The task that runs everything isère does. |

Further sizes have defaults in the headers and can be overridden here:
- the per-connection head and body budgets: `ISERE_HTTPD_HEAD_BUDGET` and `ISERE_HTTPD_BODY_BUDGET` in `include/httpd.h`
- the largest `fetch` response: `ISERE_FETCH_MAX_RESPONSE` in `src/fetch.c`
- the handler deadline: `ISERE_HANDLER_TIMEOUT_MS` in `src/engine.c`

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

### `logger.c`, `rtc.c`, `fs.c`

- **logger**: `error`, `warning`, `info` and `debug` with a tag and a format. Serialise output if other tasks also print.
- **rtc**: get and set the date and time.
- **fs**: reserved for a configuration file and static files. Today the app is embedded at build time and nothing calls it.

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

- Build with `-DISERE_APP_DIRS="<repo>/app;<repo>/tests/e2e/app"`, so the test endpoints are there.
- Tests that need the Linux build skip themselves; these use environment variables, valgrind or the server's output.
- [`tools/hil.sh`](tools/hil.sh) wraps build, flash, boot check, the suite and the benchmark for the Pico 2. Copy it for a new board.
- After a run, `/__metrics` shows `device.heap_min_free` and `device.httpd_stack_used`. They are the evidence that the heap and stack sizes fit.
