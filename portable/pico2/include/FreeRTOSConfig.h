#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS configuration for isère on the Raspberry Pi Pico 2 (RP2350, both
 * Cortex-M33 cores, FreeRTOS SMP, the RP2350_ARM_NTZ port).
 *
 * PORTING.md (FreeRTOS configuration) classifies every setting as essential or effective,
 * says what breaks without it, and explains how this file differs from the
 * Linux one. Settings at FreeRTOS's defaults are left out.
 *
 * Besides isère's httpd task this configuration serves the USB task (TinyUSB,
 * CFG_TUSB_OS = OPT_OS_FREERTOS), lwIP's tcpip thread and its FreeRTOS sys_arch,
 * and pico-sdk's sync/time interop. Settings marked "board" are kept for them;
 * they are exercised by tools/hil.sh, not by the Linux loop. */

#include <assert.h>
#include <stdint.h>

#include "rp2040_config.h"

/* --- essential: the RP2350 port --- */

/* JS engines use floating point; the port must save FPU state on a switch */
#define configENABLE_FPU                        1
/* the non-TrustZone port, running entirely in the secure world, without MPU */
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    16

/* --- essential: SMP on both cores (board) --- */

/* the second core runs the USB task and lwIP while the first runs JS */
#define configNUMBER_OF_CORES                   2
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           1
#define configUSE_CORE_AFFINITY                 1
#define configUSE_PASSIVE_IDLE_HOOK             0
#define portSUPPORT_SMP                         1

/* pico-sdk's mutexes, semaphores and sleep_ms() cooperate with the scheduler */
#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

/* --- essential: what isère and its libraries use --- */

/* required; the USB task must run while the httpd task is busy in JS */
#define configUSE_PREEMPTION                    1

/* required; isère has no use for either hook */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

/* required; lwIP's tcpip thread runs at tskIDLE_PRIORITY + 4 */
#define configMAX_PRIORITIES                    7

/* required; sizes the idle tasks, in words */
#define configMINIMAL_STACK_SIZE                ( ( configSTACK_DEPTH_TYPE ) 70 )

/* FreeRTOS defaults to 16-bit ticks, which wrap after 65 s at 1 kHz */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* the logger, lwIP's sys_arch and TinyUSB's OSAL all take mutexes; lwIP's
 * SYS_ARCH_PROTECT is a recursive mutex and its semaphores may count (board) */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1

/* run-time stats in microseconds, for the device CPU utilisation metric
 * (platform_busy_cpu_ns(): everything but the idle tasks) */
extern uint64_t time_us_64(void);
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()        time_us_64()
#define INCLUDE_xTaskGetIdleTaskHandle          1

/* isère: vTaskDelay() while USB comes up, vTaskDelete() for tasks that end;
 * lwIP's sys_arch needs vTaskSuspend (board) */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1

/* pico-sdk's interop asks whether the scheduler is running (board) */
#define INCLUDE_xTaskGetSchedulerState          1

/* kernel misuse fails loudly in every build */
#define configASSERT( x )                       assert( x )

/* --- effective: tunable --- */

/* lwIP's timeouts, and therefore the event loop's poll() timeout, are counted in
 * ticks here (unlike Linux, where poll() takes milliseconds): 1 ms resolution
 * keeps a 1 ms JS timer at 1 ms. To be measured on the board. */
#define configTICK_RATE_HZ                      ( 1000 )

/* stack high-water marks of the httpd, USB and tcpip tasks, to size their stacks
 * from measurements (ISERE_HTTPD_TASK_STACK_SIZE, TCPIP_THREAD_STACKSIZE) */
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#endif /* FREERTOS_CONFIG_H */
