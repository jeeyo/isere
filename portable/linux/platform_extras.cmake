target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
)

target_link_options(isere PRIVATE -lm -ldl -pthread)
