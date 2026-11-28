# Vendored NXP and Arm sources

The FRDM-K64F port needs a few dozen files from NXP's MCUXpresso SDK, copied here unmodified so the build doesn't depend on the multi-gigabyte SDK repositories.

| Folder | Source | Commit | Licence |
|---|---|---|---|
| `device/`, `drivers/` | [nxp-mcuxpresso/mcux-sdk](https://github.com/nxp-mcuxpresso/mcux-sdk) `devices/MK64F12/`, `drivers/{common,enet,uart,port,gpio,rnga,sysmpu,smc}/` | `8a289764d763ad06e0c3a05c885644ed98b970af` | BSD-3-Clause |
| `board/clock_config.*` | the same repository, `boards/frdmk64f/` | `8a289764d763ad06e0c3a05c885644ed98b970af` | BSD-3-Clause |
| `board/pin_mux.*` | [nxp-mcuxpresso/mcux-sdk-examples](https://github.com/nxp-mcuxpresso/mcux-sdk-examples) `frdmk64f/cmsis_driver_examples/enet/txrx_transfer/` (UART0 and ENET pins) | `52b428258efda7d5bd8a2ace2195ca828356743a` | BSD-3-Clause |
| `CMSIS/` | [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5) `CMSIS/Core/Include/` | `55b19837f5703e418ca37894d5745b1dc05e4c91` | Apache-2.0 |

isère's own linker script, `../isere_flash.ld`, is derived from `device/MK64FN1M0xxx12_flash.ld`; its header lists the changes.
