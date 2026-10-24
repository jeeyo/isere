# FreeRTOS configuration

This covers every setting in [`portable/linux/include/FreeRTOSConfig.h`](../portable/linux/include/FreeRTOSConfig.h) and [`portable/pico2/include/FreeRTOSConfig.h`](../portable/pico2/include/FreeRTOSConfig.h). Each one is classified as:

- **essential**: isère doesn't build or work correctly without this value. The table says what breaks.
- **effective**: a tunable that measurably changes performance or memory. The table gives the chosen value and the measurement behind it.
- **default**: should stay at FreeRTOS's default. These settings were removed from the files. They're listed at the end so nothing is unaccounted for.

Measurements come from the Linux validation loop: Docker on an Apple-silicon Mac, aarch64, at commit `07b20d0`. Use [`tools/freertos_measure.sh`](../tools/freertos_measure.sh) for the tick and preemption variants, and the `httpd_stack_used` field of `/__metrics` for stacks. The Pico 2 hasn't been measured yet. Rows that depend on the board say so, and `tools/hil.sh` is where those numbers will come from.

## What isère uses from FreeRTOS

Very little. Since the request path was rebuilt around one event loop, everything runs on the **httpd task**. That covers sockets, HTTP parsing, JS timers, `fetch` and both JS engines.

| isère uses | for |
|---|---|
| `xTaskCreate`, `vTaskStartScheduler`, `vTaskEndScheduler`, `vTaskDelete` | the httpd task, and a clean shutdown that frees everything (valgrind relies on it) |
| `vTaskDelay` | waiting for the network to come up; JerryScript's `jerry_port_sleep` |
| one mutex | the logger, which serialises output |
| `pvPortMalloc`/`vPortFree` (heap_3) | underneath isère's accounting allocator (`src/mem.c`) |
| run-time stats (board only) | the CPU-utilisation metric |

FreeRTOS software timers are no longer used. JS timers used to be FreeRTOS timers whose callbacks ran JS on the timer task, concurrently with the httpd task. They're now part of the event loop (`src/loop.c`).

On the board, three other users have their own needs:
- TinyUSB (`CFG_TUSB_OS = OPT_OS_FREERTOS`) runs in the USB task.
- lwIP runs its `tcpip` thread at `tskIDLE_PRIORITY + 4` and uses the FreeRTOS `sys_arch`.
- pico-sdk interoperates with FreeRTOS for sync and time.

## Linux (`portable/linux/include/FreeRTOSConfig.h`)

The FreeRTOS POSIX port runs each task on a pthread and the tick on a `SIGALRM`.

| Setting | Value | Class | Why / what breaks / measurement |
|---|---|---|---|
| `configUSE_PREEMPTION` | 1 | essential | Required by FreeRTOS.h. Without preemption, throughput drops 5% (89.5k vs 94k req/s) and a request waiting on a 1 ms JS timer takes 2.83 ms instead of 1.8 ms (measured). On the board, the USB task couldn't run while JS is busy. |
| `configUSE_IDLE_HOOK` | 0 | essential | Required by FreeRTOS.h. isère has no idle work. The empty hooks were removed from `src/hooks.c`. |
| `configUSE_TICK_HOOK` | 0 | essential | Required by FreeRTOS.h. No tick work. |
| `configMAX_PRIORITIES` | 4 | essential | Required. The httpd task runs at `tskIDLE_PRIORITY + 2`, so 3 would do; 4 leaves room. The board needs more (see below). |
| `configMINIMAL_STACK_SIZE` | `PTHREAD_STACK_MIN` words | essential | Required. In the POSIX port it only sizes the idle task and the default httpd stack (`ISERE_HTTPD_TASK_STACK_SIZE`). The port never gives a thread less than glibc's `PTHREAD_STACK_MIN` anyway, which is 128 KB on aarch64. |
| `configSTACK_DEPTH_TYPE` | `uint32_t` | essential | `PTHREAD_STACK_MIN` words doesn't fit the 16-bit depth type on hosts where it's 128 KB. |
| `configTICK_TYPE_WIDTH_IN_BITS` | `TICK_TYPE_WIDTH_32_BITS` | essential | FreeRTOS **defaults to 16-bit ticks**, which wrap after 65 s at 1 kHz. Replaces the deprecated `configUSE_16_BIT_TICKS 0`, which did the same through a compatibility path. |
| `configUSE_MUTEXES` | 1 | essential | The logger's mutex (`xSemaphoreCreateMutex`). Without it the build fails. |
| `INCLUDE_vTaskDelay` | 1 | essential | httpd waits for the network with it, as does JerryScript's port. |
| `INCLUDE_vTaskDelete` | 1 | essential | The httpd task deletes itself at shutdown. Without it the shutdown can't complete, and valgrind can't see a clean exit. |
| `configASSERT(x)` | `assert(x)` | essential | Kernel misuse fails loudly instead of corrupting state. It costs nothing measurable, so it stays on in every build. |
| `configTICK_RATE_HZ` | **100** | effective | See the table below. On Linux, JS timers wait in `poll()`, not on ticks, so the tick only costs idle CPU. 100 Hz halves it relative to 1 kHz with the same throughput. |

### Tick rate and preemption (Linux, QuickJS, `wrk -c4` on `/`, 10 s each)

| Variant | Idle CPU (one core) | Throughput, p99 | Request waiting on a 1 ms JS timer |
|---|---:|---|---:|
| 1000 Hz | 4.1% | 94.1k req/s, 81 µs | 1.78 ms |
| **100 Hz** (chosen) | **1.5%** | 94.4k req/s, 79 µs | 1.83 ms |
| 10000 Hz | 12.0% | 90.8k req/s, 84 µs | 1.71 ms |
| 1000 Hz, no preemption | 3.9% | 89.5k req/s, 83 µs | 2.83 ms |

Every tick is a signal that interrupts the httpd task, including while it waits in `poll()`. The timer column shows that JS timers don't depend on the tick.

### Stack of the httpd task

`ISERE_HTTPD_TASK_STACK_SIZE` lives in `isere_config.h`, not in FreeRTOSConfig.h. It's covered here because task stacks are what FreeRTOS sizes. The JS engines get an explicit budget of their own (`ISERE_JS_STACK_KB` in `platform_features.cmake`):
- QuickJS uses `JS_SetMaxStackSize`.
- JerryScript uses `JERRY_STACK_LIMIT`.

Each budget is counted from where each stretch of JS work starts, so deep recursion raises a `RangeError` (500) instead of overflowing the task. Before this, JerryScript recursion crashed the process; `risky.test.mjs` now covers it.

High-water mark of the httpd task, from `/__metrics` `device.httpd_stack_used`, on 64-bit Linux:

| After | QuickJS | JerryScript |
|---|---:|---:|
| boot (prelude and every module compiled) | 21.1 KB | 5.9 KB |
| `GET /` | 21.1 KB | 5.9 KB |
| RS256 verification with the JWKS fetched over HTTPS | 36.4 KB | 9.7 KB |
| ES256 verification | 36.4 KB | 9.7 KB |
| unbounded recursion, stopped at the 64 KB JS budget | 71.1 KB | 66.4 KB |

So the stack has to hold the JS budget plus about 7 KB. The 128 KB that the Linux build gets leaves room.

**For the board:**
- The httpd stack is 24 KB with a 16 KB JS budget.
- QuickJS's boot alone uses 21 KB here. A 32-bit build needs less, but likely not half as much.
- With TLS, 36 KB.
- The Pico's httpd stack is probably too small for QuickJS. It's the first thing to measure with `tools/hil.sh`, where `httpd_stack_used` comes from `uxTaskGetStackHighWaterMark`.

## Pico 2 (`portable/pico2/include/FreeRTOSConfig.h`)

The RP2350 (two Cortex-M33 cores) runs the FreeRTOS SMP kernel through the `RP2350_ARM_NTZ` port. Rows marked *board* are needed by TinyUSB, lwIP or pico-sdk rather than by isère. They are exercised by the hardware loop, not the Linux one.

| Setting | Value | Class | Why / what breaks |
|---|---|---|---|
| `configENABLE_FPU` | 1 | essential | Both JS engines use floating point. Without FPU state saved on a context switch, tasks corrupt each other's floats. The port also `#error`s if it's undefined. |
| `configENABLE_MPU` | 0 | essential | The port requires it to be defined. isère doesn't use MPU wrappers. |
| `configENABLE_TRUSTZONE` | 0 | essential | The port requires it. The non-TrustZone port is used. |
| `configRUN_FREERTOS_SECURE_ONLY` | 1 | essential | Everything runs in the secure world, as pico-sdk boots it. |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 16 | essential | Interrupts above this may not call FreeRTOS. This value is the port's, from pico-sdk. |
| `configNUMBER_OF_CORES` | 2 | essential (board) | The second core runs the USB task and lwIP while the first runs JS. The port `#error`s on invalid values. |
| `configTICK_CORE` | 0 | essential (board) | The tick runs on core 0. Checked by the port. |
| `configRUN_MULTIPLE_PRIORITIES` | 1 | essential (board) | Tasks of different priorities run at the same time on the two cores. |
| `configUSE_CORE_AFFINITY` | 1 | essential (board) | pico-sdk pins tasks to cores. |
| `configUSE_PASSIVE_IDLE_HOOK` | 0 | essential (board) | SMP requires it to be defined. There's no passive-idle work. |
| `portSUPPORT_SMP` | 1 | essential (board) | Selects the SMP kernel paths of the port. |
| `configSUPPORT_PICO_SYNC_INTEROP` | 1 | essential (board) | pico-sdk mutexes and semaphores block through the scheduler instead of spinning. |
| `configSUPPORT_PICO_TIME_INTEROP` | 1 | essential (board) | `sleep_ms()` and friends yield to the scheduler. |
| `configUSE_PREEMPTION` | 1 | essential | Required. The USB task must run while the httpd task is busy in JS. |
| `configUSE_IDLE_HOOK`, `configUSE_TICK_HOOK` | 0 | essential | Required, and unused. They used to be 1, with empty hooks. |
| `configMAX_PRIORITIES` | 7 | essential | Required. lwIP's `tcpip` thread runs at `tskIDLE_PRIORITY + 4` (`lwipopts.h`), so at least 5 are needed. |
| `configMINIMAL_STACK_SIZE` | 70 words | essential | Required. It sizes the idle tasks (one per core). |
| `configTICK_TYPE_WIDTH_IN_BITS` | 32 bits | essential | The 16-bit default wraps after 65 s. This replaces `configUSE_16_BIT_TICKS 0`. |
| `configUSE_MUTEXES` | 1 | essential | The logger, lwIP's `sys_arch` and TinyUSB's OSAL take mutexes. |
| `configUSE_RECURSIVE_MUTEXES` | 1 | essential (board) | lwIP's `SYS_ARCH_PROTECT` uses a recursive mutex in its FreeRTOS port. |
| `configUSE_COUNTING_SEMAPHORES` | 1 | essential (board) | lwIP's `sys_sem_new` can create counting semaphores. Kept until the board shows otherwise. |
| `configGENERATE_RUN_TIME_STATS`, `configRUN_TIME_COUNTER_TYPE`, `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()`, `portGET_RUN_TIME_COUNTER_VALUE()` | 1, `uint64_t`, empty, `time_us_64()` | essential | The device CPU-utilisation metric (`platform_busy_cpu_ns()`) is all time minus the idle tasks' run time. The run time is counted in microseconds; a 64-bit counter doesn't wrap after 71 minutes. |
| `INCLUDE_xTaskGetIdleTaskHandle` | 1 | essential | `ulTaskGetIdleRunTimeCounter()` exists only with it. |
| `INCLUDE_vTaskDelay`, `INCLUDE_vTaskDelete` | 1 | essential | As on Linux. |
| `INCLUDE_vTaskSuspend` | 1 | essential (board) | lwIP's FreeRTOS `sys_arch` requires it. It also lets `portMAX_DELAY` mean "forever". |
| `INCLUDE_xTaskGetSchedulerState` | 1 | essential (board) | pico-sdk's interop checks whether the scheduler runs before blocking. |
| `configASSERT(x)` | `assert(x)` | essential | As on Linux. |
| `configTICK_RATE_HZ` | 1000 | effective | Unlike Linux, lwIP's timeouts (and therefore the event loop's `lwip_poll()` timeout) are counted in ticks. At 100 Hz, a 1 ms JS timer would take 10 ms. To be measured on the board. |
| `INCLUDE_uxTaskGetStackHighWaterMark` | 1 | effective | Supplies `httpd_stack_used` on the board (see "Stack of the httpd task"), and lets the USB and `tcpip` stacks be sized from measurements. |

Software timers (`configUSE_TIMERS` and the four settings that come with it) are now off on the board too. isère no longer uses them, and neither do lwIP's `sys_arch`, TinyUSB's OSAL or the RP2350 port, which references timer settings only from its MPU wrappers (MPU is off). This saves a task, its stack and its command queue. It's a change the hardware loop has to confirm.

## Why the two files differ

| Difference | Reason |
|---|---|
| Tick rate: 100 Hz on Linux, 1000 Hz on the board | On Linux the event loop's `poll()` takes a timeout in milliseconds, so ticks only cost idle CPU. On the board, lwIP counts its timeouts in ticks. |
| `configMAX_PRIORITIES`: 4 vs 7 | lwIP's `tcpip` thread at `tskIDLE_PRIORITY + 4` exists only on the board. |
| Port and SMP settings (FPU, MPU, TrustZone, syscall priority, cores, affinity, pico interop) | Only the RP2350 port has them. The POSIX port runs on host threads. |
| Recursive mutexes, counting semaphores, `vTaskSuspend`, scheduler state | Used by lwIP, TinyUSB and pico-sdk, which only the board links. |
| Run-time stats | Linux reads the process CPU clock instead (`CLOCK_PROCESS_CPUTIME_ID`). |
| `configMINIMAL_STACK_SIZE`, `configSTACK_DEPTH_TYPE` | On Linux, the pthread stack is what counts, and glibc's minimum is 128 KB on aarch64. On the board the value is what the idle tasks really get. |

## Settings at their defaults (removed)

These were in one or both files and equal FreeRTOS's default, or turned on something nothing uses. They were removed. The default is what applies now.

| Setting | Was | Default | Note |
|---|---|---|---|
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 0 | 0 | |
| `configUSE_DAEMON_TASK_STARTUP_HOOK` | 1 | 0 | Its hook was empty; there's no timer task anymore. |
| `configMAX_TASK_NAME_LEN` | 12 | 16 | Four bytes per task. |
| `configUSE_TRACE_FACILITY` | 1 | 0 | Nothing reads task state lists. |
| `configUSE_16_BIT_TICKS` | 0 | (deprecated) | Replaced by `configTICK_TYPE_WIDTH_IN_BITS`. |
| `configIDLE_SHOULD_YIELD` | 1 | 1 | |
| `configCHECK_FOR_STACK_OVERFLOW` | 0 | 0 | Worth turning on (2) on the board while sizing stacks. That needs `vApplicationStackOverflowHook`. |
| `configQUEUE_REGISTRY_SIZE` | 15 | 0 | Debugger aid only. |
| `configUSE_APPLICATION_TASK_TAG` | 1 | 0 | Unused. |
| `configUSE_COUNTING_SEMAPHORES` (Linux) | 1 | 0 | Unused on Linux. Kept on the board. |
| `configUSE_RECURSIVE_MUTEXES` (Linux) | 1 | 0 | Unused on Linux. Kept on the board. |
| `configUSE_ALTERNATIVE_API` | 0 | 0 | |
| `configUSE_QUEUE_SETS` | 1 | 0 | Unused. |
| `configUSE_TASK_NOTIFICATIONS` | 1 | 1 | |
| `configSUPPORT_STATIC_ALLOCATION` | 0 | 0 | The static memory callbacks for the idle and timer tasks were dead code and were removed. |
| `configSUPPORT_DYNAMIC_ALLOCATION` | 1 | 1 | |
| `configUSE_TIMERS` (+ priority, queue length, stack depth, `INCLUDE_xTimerPendFunctionCall`, `INCLUDE_xTimerGetTimerDaemonTaskHandle`) | 1 | 0 | JS timers run on the event loop now. |
| `configUSE_CO_ROUTINES`, `configMAX_CO_ROUTINE_PRIORITIES` | 0, 2 | 0, (unused) | |
| `configUSE_STATS_FORMATTING_FUNCTIONS` | 0 | 0 | |
| `configSTACK_DEPTH_TYPE` (board) | `uint32_t` | `StackType_t`, 32 bits on Cortex-M | Same width. |
| `INCLUDE_vTaskPrioritySet`, `INCLUDE_vTaskCleanUpResources`, `INCLUDE_xSemaphoreGetMutexHolder` | 0 | 0 | |
| `INCLUDE_uxTaskPriorityGet`, `INCLUDE_vTaskDelayUntil`, `INCLUDE_uxTaskGetStackHighWaterMark2`, `INCLUDE_xTaskGetHandle`, `INCLUDE_eTaskGetState`, `INCLUDE_xTaskAbortDelay` | 1 | 0 | Unused. |
| `INCLUDE_vTaskSuspend`, `INCLUDE_xTaskGetSchedulerState`, `INCLUDE_xTaskGetIdleTaskHandle`, `INCLUDE_uxTaskGetStackHighWaterMark` (Linux) | 1 | 0 | Unused on Linux. Kept on the board where they're needed. |
| `INCLUDE_xTaskGetCurrentTaskHandle` | 1 | 1 | |
| `configTOTAL_HEAP_SIZE` | commented out | (heap_3 ignores it) | The heap is the C library's. isère's budget is `ISERE_HEAP_SIZE` on Linux and the linker's heap on the board. |
