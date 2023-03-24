Isere
-----

A serverless platform aimed to be running on Microcontrollers

### Current progress

- [x] FreeRTOS as Kernel
- [x] QuickJS as JavaScript runtime
- [x] (single-user) HTTP server
- [ ] Port to Raspberry Pi Pico (RP2040) (FreeRTOS-POSIX + LwIP)
- [ ] Port to ESP32?
- [ ] MicroPython?

Limitations
-----------

- No Keep-Alive support
- 1024 byte limit on each header value (please be reminded that this affects your cookie size)

Simple Guide
------------

```sh
# install dependencies
brew install make
git clone <this f***ing repo>
git submodule update --init

# compile js example
cd examples/
make -j

# compile isere
cd ..
make -j

# run isere
./isere
```

try to access `http://localhost:8080/` and see the process logs  
  
feel free to try modify `examples/echo.esm.js` (don't forget to recompile it)
