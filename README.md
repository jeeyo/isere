# isère

![workflow](https://github.com/jeeyo/isere/actions/workflows/ci.yml/badge.svg)

A serverless platform aimed to be running on Microcontrollers, powered by FreeRTOS, LwIP, and QuickJS

### Current progress

- [x] FreeRTOS as Kernel
- [x] JavaScript runtime
  - [x] QuickJS
  - [x] JerryScript
- [ ] Python runtime (?)
  - [ ] MicroPython
- [x] HTTP server
  - [x] Event Loop (no Keep-Alive support)
    - [x] Socket
    - [x] QuickJS
  - [ ] Static Files (?)
- [x] Unit tests
  - [x] loader
  - [ ] js
  - [ ] httpd
  - [ ] http handler
  - [ ] logger
- [x] Unit tests on CI
- [ ] File System
- [ ] Configuration File
- [ ] Watchdog timer
- [ ] Integration tests
- [ ] Integration tests on CI
- [ ] APIs (ref. [Minimum Common Web Platform API](https://common-min-api.proposal.wintercg.org/))
  - [ ] buffer
  - [ ] crypto
  - [ ] events
  - [ ] path (?)
  - [ ] fs
  - [ ] fetch
  - [ ] base64
  - [x] setTimeout / clearTimeout (FreeRTOS Timer)
- [ ] Memory Leak Check
- [ ] Valgrind
- [ ] Optimization
  - [ ] less libc usage + buffered printf()
  - [ ] use another data structure for httpd and tcp sockets
  - [ ] construct pollfd with less loops
- [ ] Project Template
- [ ] Low-power mode
- [x] Benchmark
- [ ] Doxygen
- [ ] Port
  - [x] Raspberry Pi Pico 2 (RP2350)
  - [ ] ESP32?
- [ ] Monitoring
  - [ ] CPU Usage ([vTaskGetRunTimeStats](https://www.freertos.org/rtos-run-time-stats.html))
  - [ ] Memory Usage ([vPortGetHeapStats](https://www.freertos.org/a00111.html))

### Limitations

- No Keep-Alive support
- JavaScript handler function needs to be stored sequentially and addressible in a memory
- File system is for storing static files and configuration files

### Building and Running

Prerequisites:
- cmake
- make
- gcc
- cpputest
- xxd
- ninja (optional for building c-capnproto)

### Install dependencies

#### macOS

```zsh
brew install gcc cmake make libtool ninja
```

If you want to build unit tests, you also need to install CppUTest

```zsh
brew install cpputest
export CPPUTEST_HOME=/opt/homebrew/Cellar/cpputest/4.0/
```

#### Debian-based Linux

```bash
sudo apt-get install -y build-essential make cmake xxd ninja-build
```

For installing CppUTest, please follow [Using CppUTest with MakefileWorker.mk and gcc](https://cpputest.github.io/) section on CppUTest website.

### Building

```sh
git clone https://github.com/jeeyo/isere.git
git submodule update --init

mkdir build
cd build
cmake -DTARGET_PLATFORM=linux -DDEBUG=on .. # see Build configurations for more options
make -j
```

#### Build configurations

|Name|Description|Supported values|
|-|-|-|
|TARGET_PLATFORM|Target platform to build isère executable for|linux (default), pico2|
|DEBUG|Whether to build isère executable with debug symbol|off (default), on|

### Running

```sh
./isere
```

A web server will start on port 8080 with the function defined in [js/handler.js](js/handler.js)

### Running Tests

```sh
./unittests
```

### Benchmark

See [BENCHMARK.md](BENCHMARK.md)

### Acknowledgment

Special thanks to

- [maxnet](https://github.com/maxnet/pico-webserver/) for tinyusb RNDIS to LwIP glue for Raspberry Pi Pico
- [libuv](https://github.com/libuv/libuv) for [src/internals/uv_poll.c](src/internals/uv_poll.c)
