set(WITH_LWIP OFF)
set(SUPPORT_DYNLINK OFF)

SET(JERRY_GLOBAL_HEAP_SIZE "(70)" CACHE STRING "")

set(ESPIDF_DIR 3rdparty/esp-idf)
include(${ESPIDF_DIR}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
