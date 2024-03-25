option(NEED_FREERTOS_POSIX "" false)
option(NEED_LWIP "" false)
option(SUPPORT_DYNLINK "" true)

target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
)
