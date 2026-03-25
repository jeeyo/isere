# isère

A serverless platform for microcontrollers, powered by Zephyr RTOS, Rust, and QuickJS.

Handlers are written in JavaScript (ES modules with async/await) and evaluated on-device by QuickJS. The device exposes an HTTP server over USB Ethernet (CDC-ECM), accepting requests and dispatching them through the JS handler — similar to AWS Lambda's programming model.

## Architecture

```
HTTP request (USB Ethernet)
  → Rust HTTP parser (httpd.rs)
    → JS handler evaluation (QuickJS via FFI)
      → HTTP response
```

- **Zephyr RTOS** — kernel, USB device stack, networking, DHCP server
- **Rust** — HTTP server, event loop, request routing, platform abstraction
- **QuickJS** — JavaScript runtime (C library linked via FFI)
- **Target** — Raspberry Pi Pico 2 (RP2350, Cortex-M33)

## Handler format

Handlers follow an AWS Lambda-like signature:

```js
export const handler = async function(event, context, done) {
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: { key: 'value' }
  }
}
```

The handler receives `event` (HTTP request with method, path, headers, query, body), `context` (function metadata), and an optional `done` callback for explicit completion.

## Project structure

```
├── CMakeLists.txt           # Zephyr build entry point
├── Cargo.toml               # Rust crate configuration
├── prj.conf                 # Zephyr Kconfig
├── west.yml                 # West manifest (Zephyr SDK + modules)
├── boards/                  # Board-specific Kconfig + devicetree overlays
│   ├── rpi_pico2_rp2350a_m33.conf / .overlay
│   └── native_sim.conf / .overlay
├── c_libs/
│   ├── CMakeLists.txt       # QuickJS Zephyr library build
│   ├── quickjs_shim.c       # FFI shims for static inline functions
│   └── quickjs/             # git submodule: github.com/bellard/quickjs
├── js/
│   ├── handler.js           # JavaScript handler source
│   └── handler.bin          # Pre-compiled bytecode (optional)
├── scripts/
│   ├── compile_bytecode.c   # Host tool: compile JS → QuickJS bytecode
│   └── compile_bytecode.sh  # Build + run the bytecode compiler
└── src/
    ├── lib.rs               # Entry point, server loop, connection state machine
    ├── httpd.rs             # HTTP/1.1 parser and response builder
    ├── http_handler.rs      # Request → QuickJS → response bridge
    ├── event_loop.rs        # Poll-based I/O event loop
    ├── js/
    │   ├── quickjs_ffi.rs   # QuickJS C API FFI bindings
    │   ├── context.rs       # JS runtime wrapper (allocator, eval, poll)
    │   └── polyfills.rs     # setTimeout / clearTimeout
    └── platform/
        ├── tcp.rs           # Zephyr POSIX socket wrapper
        ├── loader.rs        # Handler loading (source or bytecode)
        ├── logger.rs        # Zephyr printk logging
        └── rtc.rs           # Monotonic clock via k_uptime_get()
```

## Prerequisites

- [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
- [west](https://docs.zephyrproject.org/latest/develop/west/index.html) (Zephyr meta-tool)
- Rust toolchain with `thumbv8m.main-none-eabihf` target
- For bytecode compilation: 32-bit libc dev package (`gcc-multilib`)

## Building

```sh
# Clone and initialize submodules
git clone https://github.com/jeeyo/isere-c.git
cd isere-c
git submodule update --init

# Set up Zephyr workspace
west init -l .
west update

# Build for Raspberry Pi Pico 2
west build -b rpi_pico2/rp2350a/m33

# Or build for native_sim (development/testing without hardware)
west build -b native_sim
```

### Flashing

```sh
west flash
```

### Bytecode compilation (optional)

Pre-compile the JS handler to QuickJS bytecode for faster startup:

```sh
./scripts/compile_bytecode.sh
# Then build with bytecode feature:
west build -b rpi_pico2/rp2350a/m33 -- -DEXTRA_CONF_FILE=bytecode.conf
```

The bytecode compiler must be built for 32-bit to match the RP2350's pointer size.

## Development with native_sim

For rapid iteration without hardware, use Zephyr's `native_sim` target with a TAP network interface:

```sh
# Build
west build -b native_sim

# Run (requires TAP interface setup)
west build -t run
```

## Acknowledgments

- [QuickJS](https://bellard.org/quickjs/) by Fabrice Bellard — JavaScript engine
- [Zephyr RTOS](https://zephyrproject.org/) — real-time operating system
