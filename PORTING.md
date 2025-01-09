# Porting isère to a new platform

CMakeLists.txt determines the target platform to build isère for by the name of the folder containing the platform-specific source files in [src/portable/](src/portable/).
So, to port isère to a new platform, you can simply create a new folder in [src/portable/](src/portable/) and implement the following modules.

## fs
Planned to be for storing static files or configuration files. Currently not implemented.

## loader

The module that is responsible for loading JavaScript code from a storage.

- [Linux] JavaScript file will be compiled as a dynamic library and loaded with `dlopen()`
- [RP2350] JavaScript file will be statically linked with the main firmware

## logger

The module that isère uses to log messages in different levels.

## platform

Platform-specific functions e.g. board initialization, etc.

## rtc

A module to get / set the current time.

## tcp

A module that is responsible for network-related operations, provides TCP socket API.

- [Linux] Relies on BSD socket API
- [RP2350] Uses LwIP Socket API, also handle DHCP Server over USB RNDIS
