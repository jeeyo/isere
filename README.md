# Isere

A serverless platform aimed to be running on Microcontrollers

The goal is to create a low-power serverless platform that can handle simple stateless request like logging in which will require accessing to database and create JWT token

### Current progress

- [x] FreeRTOS as Kernel
- [x] QuickJS as JavaScript runtime
- [x] HTTP server
  - [ ] Event Loop
- [x] Unit tests
  - [x] loader
  - [x] js
  - [ ] logger
- [x] Unit tests on CI
- [x] File System Abstraction
- [ ] OTA
  - [ ] Update JS Function DLL
  - [ ] Configuration
- [ ] Integration tests
  - [ ] httpd
- [ ] Integration tests on CI
- [ ] Node-like APIs
  - [ ] net
  - [ ] crypto
  - [ ] fs
- [ ] QuickJS Memory Leak Check
- [ ] Use less printf()
- [ ] QuickJS Project Template
- [ ] Low-power mode
- [ ] Benchmark
- [ ] Port to Raspberry Pi Pico (RP2040) (LittleFs + FreeRTOS+POSIX + FreeRTOS+TCP)
- [ ] Port to ESP32?
- [ ] MicroPython?

### Limitations

- No Keep-Alive support
- 1024 byte limit on each header value (please be reminded that this affects your cookie size)

### Building and Running

Prerequisites: Node.js and Homebrew (macOS)

```sh
# install dependencies
brew install autoconf automake libtool

git clone <this f***ing repo>
git submodule update --init

# compile js example
cd examples/
make -j

cd ..

# compile llhttp
make -j llhttp

# compile isere
make -j

# run isere
./isere
```

try to access `http://localhost:8080/` and see the process logs  
  
feel free to try modify `examples/echo.esm.js` (don't forget to recompile it)

### Running Tests

```sh
make -j cpputest
make test
./test
```
