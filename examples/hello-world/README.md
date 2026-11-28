# isère hello world

A starting point for your own [isère](https://github.com/jeeyo/isere) app: HTTP endpoints written in JavaScript, built into firmware for a microcontroller board (or into a Linux binary while you develop).

```
CMakeLists.txt        your project: includes isère, builds the app into firmware
isere_import.cmake    finds isère and sets up the board's toolchain (copied from isère, keep it identical)
app/routes.json       routes and config
app/hello.js          GET / : one endpoint
isere/                isère, as a git submodule
```

## Get the code

Create your repository with **Use this template** on GitHub, then clone it and check out isère with only the submodules it needs. Don't use `git submodule update --recursive`: it would also pull the Raspberry Pi Pico SDK and its submodules, which only a Pico 2 build needs.

```sh
git submodule update --init isere
git -C isere submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls 3rdparty/lwip
```

isère can also live elsewhere: set `ISERE_PATH` (as `-DISERE_PATH=...` or an environment variable). Without a submodule or `ISERE_PATH`, `isere_import.cmake` fetches isère from GitHub at `ISERE_FETCH_FROM_GIT_TAG` (default `main`).

## Build and run on your computer (Linux)

```sh
cmake -B build                       # QuickJS by default; -DJS_RUNTIME=jerryscript for the other engine
cmake --build build
./build/hello_world &
curl http://127.0.0.1:8080/          # hello from isère
```

## Build for a board

The board is chosen when the build directory is created, because it decides the compiler. Use a new build directory per board.

```sh
cmake -B build-k64f -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript
cmake --build build-k64f             # build-k64f/hello_world.elf and .bin
```

| Board | `ISERE_BOARD` | Engine | Notes |
|---|---|---|---|
| NXP FRDM-K64F | `frdm-k64f` | `jerryscript` (QuickJS does not fit its 256 KB of RAM) | Needs `arm-none-eabi-gcc`: on `PATH`, in `ARM_TOOLCHAIN_PATH`, or unpacked from Arm's release into a `.toolchain/` folder here. Also needs `3rdparty/lwip`. |
| Raspberry Pi Pico 2 | `pico2` | `quickjs` or `jerryscript` | Not yet validated on hardware. Needs isère's `3rdparty/pico-sdk` submodule. |
| Linux | `linux` (default) | either | For development and tests. |

Flash the FRDM-K64F through its OpenSDA USB port with OpenOCD:

```sh
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/kx.cfg \
  -c init -c "kinetis mdm halt" -c "program build-k64f/hello_world.elf verify" -c "reset run" -c shutdown
```

The board gets its address by DHCP over Ethernet and prints `isere: ip A.B.C.D` on its UART (115200 baud, the OpenSDA virtual COM port).

## Writing the app

- `app/routes.json` maps methods and paths to JavaScript modules (or static files), and its `"config"` object reaches every handler as `env`.
- Each `*.js` file in `app/` is a module. A handler exports a Workers-style `fetch(request, env)` or a Lambda-style `handler(event, context)`.
- More directories can be added: `isere_add_app(hello_world APP_DIRS app more)`; later directories' config wins.

isère's own app (JWT verification against a JWKS) is a larger example: see `isere/app/`. Its [README](https://github.com/jeeyo/isere#readme), [PORTING.md](https://github.com/jeeyo/isere/blob/main/PORTING.md) and [BENCHMARK.md](https://github.com/jeeyo/isere/blob/main/BENCHMARK.md) cover the rest.

## Updating isère

```sh
git -C isere fetch && git -C isere checkout <tag-or-commit>
git -C isere submodule update --init 3rdparty/FreeRTOS 3rdparty/quickjs 3rdparty/jerryscript 3rdparty/mbedtls 3rdparty/lwip
cmp isere_import.cmake isere/cmake/isere_import.cmake || cp isere/cmake/isere_import.cmake .
git commit -am "Update isère"
```
