CC := gcc
CPP := g++
BIN := isere

BUILD_DIR := ./build
TEST_BUILD_DIR := ./build/tests

FREERTOS_DIR := ./3rdparty/FreeRTOS
# FREERTOS_POSIX_DIR := ./3rdparty/FreeRTOS-Plus-POSIX
QUICKJS_DIR := ./3rdparty/quickjs
LWIP_DIR := ./3rdparty/lwip
LLHTTP_DIR := ./3rdparty/llhttp
CPPUTEST_DIR := ./3rdparty/cpputest

INCLUDE_DIRS += -I./include
INCLUDE_DIRS += -I./portable/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include/private
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
INCLUDE_DIRS += -I${QUICKJS_DIR}
# INCLUDE_DIRS += -I${LWIP_DIR}/src/include
INCLUDE_DIRS += -I${LLHTTP_DIR}/build

SOURCE_FILES := $(filter-out src/main.c, $(wildcard src/*.c))
SOURCE_FILES += $(wildcard portable/src/*.c)
SOURCE_FILES += $(wildcard ${FREERTOS_DIR}/*.c)
SOURCE_FILES += ${FREERTOS_DIR}/portable/MemMang/heap_3.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
# SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_mutex.c
# SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_utils.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs-libc.c
SOURCE_FILES += ${QUICKJS_DIR}/libbf.c
SOURCE_FILES += ${QUICKJS_DIR}/libregexp.c
SOURCE_FILES += ${QUICKJS_DIR}/libunicode.c
SOURCE_FILES += ${QUICKJS_DIR}/cutils.c
# SOURCE_FILES += $(wildcard ${LWIP_DIR}/src/api/*.c)
# SOURCE_FILES += $(wildcard ${LWIP_DIR}/src/core/ipv4/*.c)
# SOURCE_FILES += $(wildcard ${LWIP_DIR}/src/core/*.c)
# SOURCE_FILES += ${LWIP_DIR}/src/netif/ethernet.c
# SOURCE_FILES += $(wildcard ${LWIP_DIR}/src/netif/ppp/*.c)
# SOURCE_FILES += $(wildcard ${LWIP_DIR}/src/netif/ppp/polarssl/*.c)
SOURCE_FILES += $(wildcard ${LLHTTP_DIR}/src/native/*.c)
SOURCE_FILES += ${LLHTTP_DIR}/build/c/llhttp.c

CFLAGS := -ggdb3 -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"$(shell git rev-parse --short HEAD)\"
LDFLAGS := -ggdb3 -pthread -ldl -lm

OBJ_FILES = $(SOURCE_FILES:%.c=$(BUILD_DIR)/%.o)

# building the main executable
MAIN_SOURCE_FILES := src/main.c
MAIN_OBJ_FILES = $(MAIN_SOURCE_FILES:%.c=$(BUILD_DIR)/%.o)

${BIN}: ${OBJ_FILES} ${MAIN_OBJ_FILES}
	-mkdir -p ${@D}
	$(CC) $^ ${LDFLAGS} -o $@

${BUILD_DIR}/%.o: %.c
	-mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -MMD -c $< -o $@

# building the test executable
TEST_INCLUDE_DIRS = -I${CPPUTEST_DIR}/include
TEST_SOURCE_FILES := $(wildcard tests/*.cpp)
TEST_SOURCE_FILES += $(wildcard ${CPPUTEST_DIR}/src/CppUTest/*.cpp)
TEST_SOURCE_FILES += $(wildcard ${CPPUTEST_DIR}/src/Platforms/Gcc/*.cpp)

TEST_OBJ_FILES += $(TEST_SOURCE_FILES:%.cpp=$(TEST_BUILD_DIR)/%.o)

tests: ${OBJ_FILES} ${TEST_OBJ_FILES}
	-mkdir -p ${@D}
	$(CPP) $^ ${LDFLAGS} -o ./test

${TEST_BUILD_DIR}/%.o: %.cpp
	-mkdir -p $(@D)
	$(CPP) $(CPPFLAGS) $(TEST_INCLUDE_DIRS) -MMD -c $< -o $@

.PHONY: clean

llhttp:
	cd $(LLHTTP_DIR) && npm install
	cd $(LLHTTP_DIR) && make

cpputest:
	cd $(CPPUTEST_DIR) && autoreconf . -i
	cd $(CPPUTEST_DIR) && ./configure
	cd $(CPPUTEST_DIR) && make

clean:
	rm -rf $(BUILD_DIR)
	rm $(BIN)
