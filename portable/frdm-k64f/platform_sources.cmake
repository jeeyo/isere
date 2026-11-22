set(K64F_DIR ${ROOT}/portable/frdm-k64f)
set(K64F_SDK ${K64F_DIR}/sdk)

list(APPEND SOURCES
  ${K64F_DIR}/src/board.c
  ${K64F_DIR}/src/ethernetif.c
  ${K64F_DIR}/src/syscalls.c
  ${K64F_SDK}/device/startup_MK64F12.S
  ${K64F_SDK}/device/system_MK64F12.c
  ${K64F_SDK}/board/clock_config.c
  ${K64F_SDK}/board/pin_mux.c
  ${K64F_SDK}/drivers/fsl_clock.c
  ${K64F_SDK}/drivers/fsl_common.c
  ${K64F_SDK}/drivers/fsl_common_arm.c
  ${K64F_SDK}/drivers/fsl_enet.c
  ${K64F_SDK}/drivers/fsl_gpio.c
  ${K64F_SDK}/drivers/fsl_rnga.c
  ${K64F_SDK}/drivers/fsl_smc.c
  ${K64F_SDK}/drivers/fsl_sysmpu.c
  ${K64F_SDK}/drivers/fsl_uart.c
  ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
  ${FREERTOS_DIR}/portable/MemMang/heap_5.c
  ${LWIP_DIR}/contrib/ports/freertos/sys_arch.c
)

set(K64F_INCLUDE_DIRS
  ${K64F_DIR}/include
  ${K64F_SDK}/CMSIS
  ${K64F_SDK}/device
  ${K64F_SDK}/drivers
  ${K64F_SDK}/board
  ${FREERTOS_DIR}/include
  ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
  ${LWIP_DIR}/contrib/ports/freertos/include
)
set(K64F_DEFINITIONS CPU_MK64FN1M0VLL12 __STARTUP_CLEAR_BSS)

# lwIP's libraries are built from its own Filelists.cmake with these
set(LWIP_COMPILER_FLAGS )
set(LWIP_DEFINITIONS ${K64F_DEFINITIONS})
