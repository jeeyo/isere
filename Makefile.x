CC := arm-none-eabi-gcc
BIN := isere.x

BUILD_DIR := ./build

FREERTOS_DIR := ./3rdparty/FreeRTOS
FREERTOS_POSIX_DIR := ./3rdparty/FreeRTOS-Plus-POSIX
QUICKJS_DIR := ./3rdparty/quickjs

INCLUDE_DIRS += -I./include
INCLUDE_DIRS += -I./portable/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/include
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include/private
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
INCLUDE_DIRS += -I${QUICKJS_DIR}

SOURCE_FILES := $(wildcard src/*.c)
SOURCE_FILES += $(wildcard portable/src/*.c)
SOURCE_FILES += $(wildcard ${FREERTOS_DIR}/*.c)
SOURCE_FILES += ${FREERTOS_DIR}/portable/MemMang/heap_3.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/RP2040/port.c
SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_mutex.c
SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_utils.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs-libc.c
SOURCE_FILES += ${QUICKJS_DIR}/libbf.c
SOURCE_FILES += ${QUICKJS_DIR}/libregexp.c
SOURCE_FILES += ${QUICKJS_DIR}/libunicode.c
SOURCE_FILES += ${QUICKJS_DIR}/cutils.c

CFLAGS := -ggdb3
CFLAGS += $(INCLUDE_DIRS) -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"$(git rev-parse --short HEAD)\"
LDFLAGS := -ggdb3 -ldl

OBJ_FILES = $(SOURCE_FILES:%.c=$(BUILD_DIR)/%.o)

DEP_FILE = $(OBJ_FILES:%.o=%.d)

${BIN}: ${OBJ_FILES}
	-mkdir -p ${@D}
	$(CC) $^ ${LDFLAGS} -o $@

-include ${DEP_FILE}

${BUILD_DIR}/%.o: %.c
	-mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

.PHONY: clean

clean:
	-rm -rf $(BUILD_DIR)
	-rm $(BIN)
