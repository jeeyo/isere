target_include_directories(isere PRIVATE
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
  ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(isere PRIVATE Threads::Threads m ${CMAKE_DL_LIBS})
