# Isere

A serverless platform aimed to be running on Microcontrollers

The goal is to create a low-power serverless platform that can handle simple stateless request like logging in which will require accessing to database and create JWT token

### Current progress

- [x] FreeRTOS as Kernel
- [x] QuickJS as JavaScript runtime
- [x] HTTP server
  - [ ] Event Loop
  - [ ] Static Files (?)
- [x] Unit tests
  - [x] loader
  - [x] js
  - [ ] httpd
  - [ ] logger
- [x] Unit tests on CI
- [x] File System Abstraction
- [ ] OTA
  - [ ] Update JS Function DLL
  - [ ] Configuration
- [ ] Integration tests
- [ ] Integration tests on CI
- [ ] Node-like APIs (ref. [workerd](https://github.com/cloudflare/workerd/tree/main/src/node))
  - [ ] assert
  - [ ] buffer
  - [ ] crypto
  - [x] diagnostics_channel ❌
  - [ ] events
  - [ ] path
  - [x] process.env
  - [ ] stream (?)
  - [ ] string_decoder
  - [ ] util
- [ ] QuickJS Memory Leak Check
- [ ] Use less printf()
- [ ] QuickJS Project Template
- [ ] Low-power mode
- [ ] Benchmark
- [ ] Port to Raspberry Pi Pico (RP2040) (LittleFs + FreeRTOS+POSIX + FreeRTOS+TCP)
- [ ] Integrate with some Analytics and Monitoring Platforms
- [ ] Port to ESP32?
- [ ] MicroPython?

### Limitations

- No Keep-Alive support
- JavaScript handler function needs to be stored sequentially and addressible in a Flash memory
- File system is for storing static files and configuration files

### Building and Running

Prerequisites:
- Node.js (for generating llhttp C code)
- autoconf
- make
- gcc
- libtool

```sh
# install dependencies
brew install autoconf libtool
# or
sudo apt install -y build-essential libtool

git clone <this f***ing repo>
git submodule update --init

# compile dependencies
make -j deps

# compile isere
make -j

# run isere
./isere
```

try to access `http://localhost:8080/` and see the process logs  
  
feel free to try modify `examples/echo.esm.js` (don't forget to recompile it using `make -j .testjs`)

### Running Tests

```sh
make -j unittest
./unittest
```
