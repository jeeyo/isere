CC := gcc
BIN := isere

BUILD_DIR := ./build

FREERTOS_DIR := ./3rdparty/FreeRTOS
FREERTOS_POSIX_DIR := ./3rdparty/FreeRTOS-Plus-POSIX
QUICKJS_DIR := ./3rdparty/quickjs
LWIP_DIR := ./3rdparty/lwip

INCLUDE_DIRS += -I./include
INCLUDE_DIRS += -I./portable/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
INCLUDE_DIRS += -I${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/include/private
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include
INCLUDE_DIRS += -I${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/include/portable
INCLUDE_DIRS += -I${QUICKJS_DIR}
INCLUDE_DIRS += -I${LWIP_DIR}/src/include

SOURCE_FILES := $(wildcard src/*.c)
SOURCE_FILES += $(wildcard portable/src/*.c)
SOURCE_FILES += $(wildcard ${FREERTOS_DIR}/*.c)
SOURCE_FILES += ${FREERTOS_DIR}/portable/MemMang/heap_3.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
SOURCE_FILES += ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_pthread_mutex.c
SOURCE_FILES += ${FREERTOS_POSIX_DIR}/FreeRTOS-Plus-POSIX/source/FreeRTOS_POSIX_utils.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs.c
SOURCE_FILES += ${QUICKJS_DIR}/quickjs-libc.c
SOURCE_FILES += ${QUICKJS_DIR}/libbf.c
SOURCE_FILES += ${QUICKJS_DIR}/libregexp.c
SOURCE_FILES += ${QUICKJS_DIR}/libunicode.c
SOURCE_FILES += ${QUICKJS_DIR}/cutils.c
SOURCE_FILES += ${LWIP_DIR}/src/api/err.c
SOURCE_FILES += ${LWIP_DIR}/src/api/sockets.c
SOURCE_FILES += ${LWIP_DIR}/src/api/api_msg.c
SOURCE_FILES += ${LWIP_DIR}/src/api/api_lib.c
SOURCE_FILES += ${LWIP_DIR}/src/api/netbuf.c
SOURCE_FILES += ${LWIP_DIR}/src/api/tcpip.c
SOURCE_FILES += ${LWIP_DIR}/src/core/ipv4/ip4.c
SOURCE_FILES += ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
SOURCE_FILES += ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
SOURCE_FILES += ${LWIP_DIR}/src/core/def.c
SOURCE_FILES += ${LWIP_DIR}/src/core/mem.c
SOURCE_FILES += ${LWIP_DIR}/src/core/memp.c
SOURCE_FILES += ${LWIP_DIR}/src/core/inet_chksum.c
SOURCE_FILES += ${LWIP_DIR}/src/core/pbuf.c
SOURCE_FILES += ${LWIP_DIR}/src/core/netif.c
SOURCE_FILES += ${LWIP_DIR}/src/core/ip.c
SOURCE_FILES += ${LWIP_DIR}/src/core/init.c
SOURCE_FILES += ${LWIP_DIR}/src/core/stats.c
SOURCE_FILES += ${LWIP_DIR}/src/core/timeouts.c
SOURCE_FILES += ${LWIP_DIR}/src/core/tcp.c
SOURCE_FILES += ${LWIP_DIR}/src/core/tcp_in.c
SOURCE_FILES += ${LWIP_DIR}/src/core/tcp_out.c

CFLAGS := -ggdb3
CFLAGS += $(INCLUDE_DIRS) -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"$(git rev-parse --short HEAD)\"
LDFLAGS := -ggdb3 -pthread -ldl -lm

OBJ_FILES = $(SOURCE_FILES:%.c=$(BUILD_DIR)/%.o)

DEP_FILE = $(OBJ_FILES:%.o=%.d)

${BIN}: ${OBJ_FILES}
	-mkdir -p ${@D}
	$(CC) $^ ${LDFLAGS} -o $@

-include ${DEP_FILE}

${BUILD_DIR}/%.o: %.c
	-mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -c $< -o $@

.PHONY: clean

clean:
	-rm -rf $(BUILD_DIR)
	-rm $(BIN)
