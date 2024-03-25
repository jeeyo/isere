list(APPEND SOURCES
  ${QUICKJS_DIR}/quickjs-libc.c
  ${FREERTOS_DIR}/portable/MemMang/heap_4.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
)
