# Hardware validation loop (`hil.sh`)

`tools/hil.sh` runs one validation iteration on a real Raspberry Pi Pico 2:

1. builds the firmware with a unique build ID
2. flashes it over SWD with a Raspberry Pi Debug Probe (no BOOTSEL button, works even if the previous build crashed)
3. records the board's UART output through the probe
4. waits for the board's USB network interface (MAC `02:02:84:6a:96:00`) to get an IPv4 address
5. waits until `http://192.168.7.1:8080/__version` reports the build ID from step 1, so it can't test stale firmware
6. runs smoke tests and, optionally, your end-to-end suite (`HIL_TEST_CMD`)
7. benchmarks with `wrk` at 1, 2 and 4 connections, then checks the board still answers

Each run writes `hil-results/<build-id>/` containing `summary.json`, `hil.log`, `build.log`, `openocd.log`, `serial.log`, `tests.txt` and `bench-c*.txt`.

The exit code is 0 for passed, 1 for failed tests, and 2 when build, flash or boot failed. That makes it safe to run in a loop from an agent.

## Wiring

```
Mac ──USB── Debug Probe ──"D" (SWD) cable── Pico 2 SWD header (SWCLK / GND / SWDIO)
                        └─"U" (UART) cable── Pico 2 GP0 (UART0 TX) → probe RX
                                             Pico 2 GP1 (UART0 RX) ← probe TX
                                             GND ── GND
Mac ──USB── Pico 2 (power + CDC-ECM network, 192.168.7.1)
```

## macOS setup

```zsh
brew install cmake wrk
```

You also need:
- **ARM toolchain.** Either `brew install --cask gcc-arm-embedded`, or the toolchain the Raspberry Pi Pico VS Code extension installs under `~/.pico-sdk/toolchain/`. If you use the extension's toolchain, set `PICO_TOOLCHAIN_PATH` to it.
- **openocd with RP2350 support.** That means the Raspberry Pi fork, which the Pico VS Code extension installs under `~/.pico-sdk/openocd/<version>/openocd`. `hil.sh` finds it there automatically. Any other build works if it has `target/rp2350.cfg`; point `OPENOCD` at it. The Homebrew `open-ocd` release may not have RP2350 support yet.
- **Submodules.** Run `git submodule update --init 3rdparty/FreeRTOS 3rdparty/jerryscript 3rdparty/llhttp 3rdparty/libyuarel 3rdparty/pico-sdk`, then `git -C 3rdparty/pico-sdk submodule update --init lib/tinyusb lib/lwip`.

On Apple silicon Macs, macOS may ask "Allow accessory to connect?" the first time each USB device appears. Allow both the probe and the Pico 2.

If more than one `/dev/cu.usbmodem*` device exists, set `HIL_SERIAL` to the probe's UART.

## Examples

```zsh
tools/hil.sh                                   # full iteration, JerryScript
tools/hil.sh --runtime quickjs                 # needs a working quickjs submodule (see #33)
tools/hil.sh --no-bench                        # build, flash and smoke tests only
tools/hil.sh --skip-flash                      # re-test the firmware already on the board
HIL_TEST_CMD='pytest tests/integration' tools/hil.sh   # also run an e2e suite against $ISERE_URL
```

To try the script without hardware, run the Linux build and point the script at it: `PICO_HOST=127.0.0.1 PICO_MAC= tools/hil.sh --skip-flash`.
