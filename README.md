# isère

isère [(/iːˈzɛər/)](https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=fr&q=Isère)

A serverless platform aimed to be running on Microcontrollers, powered by FreeRTOS, LwIP, and QuickJS

### Current progress

- [x] FreeRTOS as Kernel
- [x] QuickJS as JavaScript runtime
- [x] HTTP server
  - [x] Event Loop (no Keep-Alive support)
    - [x] Socket
    - [x] QuickJS
  - [ ] Static Files (?)
- [x] Unit tests
  - [x] loader
  - [x] js
  - [ ] httpd
  - [ ] http handler
  - [ ] logger
- [x] Unit tests on CI
- [ ] File System
- [x] Configuration File
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
- [ ] gprof profiling
- [ ] Optimize libc usage
  - [ ] Use less printf()
- [ ] QuickJS Project Template
- [ ] Low-power mode
- [x] Benchmark
- [x] Port to Raspberry Pi Pico (RP2040)
  - [x] Multi-core support
- [ ] Monitoring
  - [ ] CPU Usage ([vTaskGetRunTimeStats](https://www.freertos.org/rtos-run-time-stats.html))
  - [ ] Memory Usage ([vPortGetHeapStats](https://www.freertos.org/a00111.html))
- [ ] Port to ESP32?
- [x] MicroPython (will not do ❌)

### Limitations

- No Keep-Alive support
- JavaScript handler function needs to be stored sequentially and addressible in a Flash memory
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
cmake -DTARGET_PLATFORM=linux -DDEBUG=on .. # see CMake variables for more options
make -j
```

#### Build configurations

|Name|Description|Supported values|
|-|-|-|
|TARGET_PLATFORM|Target platform to build isère executable for|linux (default), rp2040|
|DEBUG|Whether to build isère executable with debug symbol|off (default), on|
|WITH_QUICKJS|Whether to include QuickJS runtime in isère executable|off, on (default)|

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
