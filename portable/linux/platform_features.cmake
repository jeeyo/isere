set(WITH_LWIP OFF)

SET(JERRY_GLOBAL_HEAP_SIZE "(512)" CACHE STRING "")

# C stack a JS engine may use on the httpd task (whose pthread stack is 128 KB)
set(ISERE_JS_STACK_KB 64)
