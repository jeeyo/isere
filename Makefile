# CC := arm-none-eabi-gcc
# CPP := arm-none-eabi-g++
CC := gcc
CPP := g++
MAKE := make

ISERE_BIN := isere
TEST_BIN := unittest

ISERE_BUILD_DIR := ./build
TEST_BUILD_DIR := ./build/tests

FREERTOS_DIR := ./3rdparty/FreeRTOS
FREERTOS_POSIX_DIR := ./3rdparty/FreeRTOS-Plus-POSIX
QUICKJS_DIR := ./3rdparty/quickjs
LWIP_DIR := ./3rdparty/lwip
LLHTTP_DIR := ./3rdparty/llhttp
LIBYUAREL_DIR := ./3rdparty/libyuarel
CPPUTEST_DIR := ./3rdparty/cpputest
CAPNPROTO_DIR := ./3rdparty/c-capnproto

INCLUDE_DIRS += -I./include
INCLUDE_DIRS += -I./portable/include
INCLUDE_DIRS += -I./schemas
INCLUDE_DIRS += -I${FREERTOS_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include/FreeRTOS_POSIX
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include/private
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
# INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
INCLUDE_DIRS += -I${QUICKJS_DIR}/include
# INCLUDE_DIRS += -I${LWIP_DIR}/src/include
INCLUDE_DIRS += -I${LLHTTP_DIR}/include
INCLUDE_DIRS += -I${LIBYUAREL_DIR}
INCLUDE_DIRS += -I${CAPNPROTO_DIR}/lib

SOURCE_FILES := $(filter-out src/main.c, $(wildcard src/*.c))
SOURCE_FILES += $(wildcard src/polyfills/*.c)
SOURCE_FILES += $(wildcard portable/src/*.c)
SOURCE_FILES += $(wildcard schemas/*.c)
SOURCE_FILES += $(wildcard ${FREERTOS_DIR}/*.c)
SOURCE_FILES += ${FREERTOS_DIR}/portable/MemMang/heap_4.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
# SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
# SOURCE_FILES += $(wildcard ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/*.c)
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
SOURCE_FILES += $(wildcard ${LLHTTP_DIR}/src/*.c)
SOURCE_FILES += ${LIBYUAREL_DIR}/yuarel.c
SOURCE_FILES += $(wildcard ${CAPNPROTO_DIR}/lib/*.c)

QUICKJS_DEFINES := -D_GNU_SOURCE -DCONFIG_BIGNUM -DEMSCRIPTEN -DCONFIG_VERSION=\"$(shell git rev-parse --short HEAD)\"

CFLAGS := -ggdb3 ${QUICKJS_DEFINES}
LDFLAGS := -ggdb3 -nostdlib -lm
UNITTEST_LDFLAGS := -ggdb3 -ldl -lm

OBJ_FILES = $(SOURCE_FILES:%.c=$(ISERE_BUILD_DIR)/%.o)

# building the main executable
MAIN_SOURCE_FILES := src/main.c
MAIN_OBJ_FILES = $(MAIN_SOURCE_FILES:%.c=$(ISERE_BUILD_DIR)/%.o)

${ISERE_BIN}: ${OBJ_FILES} ${MAIN_OBJ_FILES}
	-mkdir -p ${@D}
	$(CC) $^ ${LDFLAGS} -o $@

${ISERE_BUILD_DIR}/%.o: %.c
	-mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -MMD -c $< -o $@

# building the test executable
TEST_INCLUDE_DIRS := -I${CPPUTEST_DIR}/include
TEST_SOURCE_FILES := $(wildcard tests/*.cpp)
TEST_SOURCE_FILES += $(wildcard ${CPPUTEST_DIR}/src/CppUTest/*.cpp)
TEST_SOURCE_FILES += $(wildcard ${CPPUTEST_DIR}/src/CppUTestExt/*.cpp)
TEST_SOURCE_FILES += $(wildcard ${CPPUTEST_DIR}/src/Platforms/Gcc/*.cpp)

TEST_OBJ_FILES := $(TEST_SOURCE_FILES:%.cpp=$(TEST_BUILD_DIR)/%.o)

${TEST_BIN}: ${OBJ_FILES} ${TEST_OBJ_FILES}
	cd ./tests/js && $(MAKE)
	-mkdir -p ${@D}
	$(CPP) $^ ${UNITTEST_LDFLAGS} -o $@

${TEST_BUILD_DIR}/%.o: %.cpp
	-mkdir -p $(@D)
	$(CPP) $(CPPFLAGS) $(INCLUDE_DIRS) $(TEST_INCLUDE_DIRS) -MMD -c $< -o $@

.PHONY: clean

deps:
	$(MAKE) .examples
	$(MAKE) .quickjs
	$(MAKE) .cpputest

.capnproto:
	cd $(CAPNPROTO_DIR) && cmake --preset=ci-linux_x86_64
	cd $(CAPNPROTO_DIR) && cmake --build --preset=ci-tests

.capnp:
	capnp compile -o 3rdparty/c-capnproto/build/capnpc-c schemas/*.capnp

.cpputest:
	cd $(CPPUTEST_DIR) && autoreconf . -i
	cd $(CPPUTEST_DIR) && ./configure
	cd $(CPPUTEST_DIR) && $(MAKE)

.quickjs:
	mkdir -p $(QUICKJS_DIR)/include
	cp $(QUICKJS_DIR)/*.h $(QUICKJS_DIR)/include

.examples:
	cd ./examples && $(MAKE)

clean:
	rm -f ./examples/*.so.c
	rm -f ./tests/js/*.so.c
	rm -rf $(TEST_BUILD_DIR)
	rm -f $(TEST_BIN)
	rm -rf $(ISERE_BUILD_DIR)
	rm -f $(ISERE_BIN)
