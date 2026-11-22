# NXP FRDM-K64F: Kinetis MK64FN1M0VLL12, Cortex-M4F at 120 MHz, 256 KB SRAM, 1 MB flash, ENET.
# Included before project(), so the cross toolchain is chosen here.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# arm-none-eabi-gcc from ARM_TOOLCHAIN_PATH, the repository's ignored .toolchain/ folder, or PATH
if(NOT ARM_TOOLCHAIN_PATH)
  file(GLOB _local_toolchain "${CMAKE_CURRENT_LIST_DIR}/../../.toolchain/arm-gnu-toolchain-*/bin")
  if(_local_toolchain)
    list(GET _local_toolchain 0 ARM_TOOLCHAIN_PATH)
  endif()
endif()
find_program(ARM_GCC arm-none-eabi-gcc HINTS ${ARM_TOOLCHAIN_PATH} REQUIRED)
get_filename_component(_arm_bin ${ARM_GCC} DIRECTORY)
set(CMAKE_C_COMPILER ${_arm_bin}/arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER ${_arm_bin}/arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER ${_arm_bin}/arm-none-eabi-gcc)
set(CMAKE_OBJCOPY ${_arm_bin}/arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE ${_arm_bin}/arm-none-eabi-size CACHE FILEPATH "")

set(K64F_CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${K64F_CPU_FLAGS} -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_ASM_FLAGS_INIT "${K64F_CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${K64F_CPU_FLAGS} --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")

set(WITH_LWIP ON)

if(WITH_OTEL)
  set(WITH_OTEL OFF)
  message(WARNING "Target 'frdm-k64f' doesn't support OpenTelemetry yet -- disabled")
endif()

# JerryScript's fixed arena, in KB, out of a ~215 KB heap. 48 KB ran out on many headers
# and on bodies at the 16 KB budget; 96 KB left too little heap for connection buffers
# and /__metrics; 80 KB still ran out echoing a 16 KB body once the test routes' modules
# were resident (body, text and JSON copies on top of the ~20 KB prelude). 96 KB, with
# the smaller network buffers and stacks below, leaves ~85 KB of heap free.
set(JERRY_GLOBAL_HEAP_SIZE "(96)" CACHE STRING "")

# environment the firmware sets at boot, "NAME=value;NAME=value" (test builds match
# tests/e2e/lib/target.mjs DEFAULT_ENV, since a board can't be given one)
set(ISERE_BOARD_ENV "" CACHE STRING "Environment variables set at boot")

# C stack a JS engine may use on the httpd task (ISERE_HTTPD_TASK_STACK_SIZE: 20 KB)
set(ISERE_JS_STACK_KB 14)
