# Validation loops

Two scripts run the same black-box test suite, `tests/e2e/*.test.mjs`. It's written with Node's built-in [`node:test`](https://nodejs.org/api/test.html) runner and has no dependencies.

| Script | Target | Use it |
|---|---|---|
| `tools/linux.sh` | Linux build on this machine | first, on every change: about a minute per iteration, including valgrind |
| `tools/hil.sh` | Raspberry Pi Pico 2 through a Debug Probe | once the Linux loop is green |

Both write `hil-results/<build-id>/`, containing:
- `summary.json`: each step's outcome and timing
- `run.log`
- `build.log`
- `tests.log` and `tests.xml` (JUnit)
- `bench-c*.txt`
- `server.log` (Linux) or `serial.log` and `openocd.log` (Pico 2)

The exit codes are 0 for passed, 1 for failed tests, and 2 for a build, flash or boot failure.

## The test suite

| File | Covers |
|---|---|
| `smoke.test.mjs` | `/__version`, the JS handler, 404, sequential requests, malformed and pipelined requests, clients disconnecting mid-request |
| `load.test.mjs` | 1, 2 and 4 concurrent clients, and still accepting connections afterwards |
| `leak.test.mjs` | valgrind on the Linux build: memory errors, plus lost and retained memory growth per request |

You can run it directly with Node 22 or newer:

```sh
node --test --test-concurrency=1 'tests/e2e/*.test.mjs'                      # against build/ (Linux)
ISERE_BUILD_DIR=build-linux-quickjs node --test --test-concurrency=1 'tests/e2e/*.test.mjs'
ISERE_URL=http://192.168.7.1:8080 node --test --test-concurrency=1 'tests/e2e/*.test.mjs'   # a running board
```

Against the Linux build, each file starts its own `isere` on port 8080 (override with `ISERE_PORT`) and stops it with SIGINT. With `ISERE_URL` set, nothing is started and the valgrind tests skip themselves. Set `ISERE_VALGRIND=0` to skip valgrind on Linux.

## `tools/linux.sh`

```sh
tools/linux.sh                          # QuickJS: build, tests with valgrind, 10 s benchmark per level
tools/linux.sh --runtime jerryscript
tools/linux.sh --no-valgrind --no-bench # quickest iteration
```

The build goes to `build-linux-<runtime>/`. You need cmake, make, xxd, curl, Node 22+, and valgrind. `wrk` is optional; without it the benchmark is skipped.

valgrind doesn't run natively on Apple silicon Macs. There, run `tools/linux.sh` in a Linux container or VM, or use `--no-valgrind` and rely on CI.

## `tools/hil.sh`

One iteration:
1. builds the firmware with a unique build ID
2. flashes it over SWD with a Raspberry Pi Debug Probe (no BOOTSEL button, works even if the previous build crashed)
3. records the board's UART output through the probe
4. waits for the board's USB network interface (MAC `02:02:84:6a:96:00`) to get an IPv4 address
5. waits until `http://192.168.7.1:8080/__version` reports the build ID from step 1, so it can't test stale firmware
6. runs the test suite against the board
7. benchmarks with `wrk` at 1, 2 and 4 connections, then checks the board still answers

### Wiring

```
Mac ──USB── Debug Probe ──"D" (SWD) cable── Pico 2 SWD header (SWCLK / GND / SWDIO)
                        └─"U" (UART) cable── Pico 2 GP0 (UART0 TX) → probe RX
                                             Pico 2 GP1 (UART0 RX) ← probe TX
                                             GND ── GND
Mac ──USB── Pico 2 (power + CDC-ECM network, 192.168.7.1)
```

### macOS setup

```zsh
brew install cmake node wrk
```

You also need:
- **ARM toolchain.** Either `brew install --cask gcc-arm-embedded`, or the toolchain the Raspberry Pi Pico VS Code extension installs under `~/.pico-sdk/toolchain/`. If you use the extension's toolchain, set `PICO_TOOLCHAIN_PATH` to it.
- **openocd with RP2350 support.** That means the Raspberry Pi fork, which the Pico VS Code extension installs under `~/.pico-sdk/openocd/<version>/openocd`. `hil.sh` finds it there automatically. Any other build works if it has `target/rp2350.cfg`; point `OPENOCD` at it. The Homebrew `open-ocd` release may not have RP2350 support yet.
- **Submodules.** Run `git submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/llhttp 3rdparty/libyuarel 3rdparty/pico-sdk`, then `git -C 3rdparty/pico-sdk submodule update --init lib/tinyusb lib/lwip`.

On Apple silicon Macs, macOS may ask "Allow accessory to connect?" the first time each USB device appears. Allow both the probe and the Pico 2.

If more than one `/dev/cu.usbmodem*` device exists, set `HIL_SERIAL` to the probe's UART.

### Examples

```zsh
tools/hil.sh                      # full iteration, QuickJS
tools/hil.sh --runtime jerryscript
tools/hil.sh --no-bench           # build, flash and tests only
tools/hil.sh --skip-flash         # re-test the firmware already on the board
```
