# isère

isère [(/iːˈzɛər/)](https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=fr&q=Isère)

A serverless platform aimed to be running on Microcontrollers

The goal is to create a low-power serverless platform that can handle simple stateless request like logging in which will require accessing to database and creating JWT token

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
- [x] File System Abstraction
- [x] Configuration File
- [ ] Integration tests (Native / QEMU)
- [ ] Integration tests on CI
- [ ] APIs (ref. [Minimum Common Web Platform API](https://common-min-api.proposal.wintercg.org/))
  - [ ] buffer
  - [ ] crypto
  - [ ] events
  - [ ] path (?)
  - [ ] fetch
  - [ ] base64
  - [x] setTimeout (FreeRTOS Timer)
- [ ] Memory Leak Check
- [ ] Valgrind
- [ ] Optimize libc usage
  - [ ] Use less printf()
- [ ] QuickJS Project Template
- [ ] Low-power mode
- [ ] Benchmark
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
- automake
- cmake
- make
- gcc
- libtool
- ninja (optional for building c-capnproto)

```sh
# install dependencies
brew install automake libtool ninja
# or
sudo apt install -y build-essential libtool ninja-build

git clone https://github.com/jeeyo/isere.git
git submodule update --init

# build
mkdir build
cd build
cmake ..
make -j

# run isere
./isere
```

try to access `http://localhost:8080/` and see the process logs  
  
feel free to try modify `examples/handler.js` (don't forget to recompile it)

### Running Tests

```sh
make -j unittest
./unittest
```
